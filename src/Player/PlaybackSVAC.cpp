#if defined(ENABLE_RTPPROXY)
#include "PlaybackSVAC.h"
#include "Rtp/RtpCache.h"
#include "Rtsp/RtspSession.h"
#include "Thread/WorkThreadPool.h"
#include "Util/uv_errno.h"
#include <cstring>

using namespace std;
using namespace toolkit;

namespace mediakit {

PlaybackSVAC::PlaybackSVAC(EventPoller::Ptr poller) {
    _poller = poller ? std::move(poller) : EventPollerPool::Instance().getPoller();
    _socket_rtp = Socket::createSocket(_poller, false);
}

PlaybackSVAC::~PlaybackSVAC() {
    stop();
}

void PlaybackSVAC::start(const string &file_path, const MediaSourceEvent::SendRtpArgs &args, onStartCB cb) {
    _args = args;
    _file_path = file_path;
    _stopped = false;
    _current_idx = 0;

    _file.reset(fopen(file_path.c_str(), "rb"), [](FILE *fp) {
        if (fp) fclose(fp);
    });
    if (!_file) {
        cb(0, SockException(Err_other, StrPrinter << "Cannot open file: " << file_path));
        return;
    }

    // Detect raw RTP dump by .rtp extension
    _raw_mode = (file_path.size() > 4 && file_path.compare(file_path.size() - 4, 4, ".rtp") == 0);

    if (_raw_mode) {
        indexRtpFile();
        if (_rtp_index.empty()) {
            cb(0, SockException(Err_other, "RTP dump file has no packets"));
            return;
        }
        _play_ticker.resetTime();
    } else {
        indexFile();
        if (_index.size() < 2) {
            cb(0, SockException(Err_other, "PS file has too few packs, need at least 2"));
            return;
        }
        _base_scr_ms = _index[0].scr_ms;
        _play_ticker.resetTime();

        // Create RTP encoder (RtpCachePS with PS passthrough)
        weak_ptr<PlaybackSVAC> weak_self = shared_from_this();
        auto lam = [weak_self](shared_ptr<List<Buffer::Ptr>> list) {
            auto strong_self = weak_self.lock();
            if (strong_self) {
                strong_self->onFlushRtpList(std::move(list));
            }
        };
        _rtp_encoder = make_shared<RtpCachePS>(lam, (uint32_t)atoi(args.ssrc.data()), args.pt, true);
    }

    weak_ptr<PlaybackSVAC> weak_self = shared_from_this();

    if (args.con_type == MediaSourceEvent::SendRtpArgs::kUdpActive) {
        startUdpActive(cb);
    } else if (args.con_type == MediaSourceEvent::SendRtpArgs::kTcpActive) {
        _socket_rtp->connect(args.dst_url, args.dst_port, [cb, weak_self](const SockException &err) {
            auto strong_self = weak_self.lock();
            if (!strong_self) { cb(0, err); return; }
            if (!err) strong_self->onConnect();
            cb(strong_self->_socket_rtp->get_local_port(), err);
        }, args.close_delay_ms ? args.close_delay_ms / 1000.0 : 5.0, "::", args.src_port);
    } else {
        cb(0, SockException(Err_other, "Unsupported connection type, only kUdpActive and kTcpActive supported"));
    }
}

void PlaybackSVAC::startUdpActive(onStartCB cb) {
    auto self = shared_from_this();
    WorkThreadPool::Instance().getPoller()->async([cb, self]() {
        struct sockaddr_storage addr;
        if (!SockUtil::getDomainIP(self->_args.dst_url.data(), self->_args.dst_port, addr, AF_INET, SOCK_DGRAM, IPPROTO_UDP)) {
            self->_poller->async([cb, self]() {
                cb(0, SockException(Err_dns, "DNS resolution failed"));
            });
            return;
        }

        self->_poller->async([cb, self, addr]() {
            string ifr_ip = addr.ss_family == AF_INET ? "0.0.0.0" : "::";
            try {
                if (self->_args.src_port) {
                    if (!self->_socket_rtp->bindUdpSock(self->_args.src_port, ifr_ip, true)) {
                        throw invalid_argument(StrPrinter << "bind udp port failed: " << self->_args.src_port);
                    }
                } else {
                    auto pr = make_pair(self->_socket_rtp, Socket::createSocket(self->_poller, false));
                    makeSockPair(pr, ifr_ip, true, true);
                }
            } catch (exception &ex) {
                cb(0, SockException(Err_other, ex.what()));
                return;
            }
            SockUtil::setBroadcast(self->_socket_rtp->rawFD());
            self->_socket_rtp->bindPeerAddr((struct sockaddr *)&addr, 0, true);
            self->onConnect();
            cb(self->_socket_rtp->get_local_port(), SockException());
        });
    });
}

void PlaybackSVAC::onConnect() {
    _is_connect = true;
    SockUtil::setSendBuf(_socket_rtp->rawFD(), 4 * 1024 * 1024);

    if (_args.con_type == MediaSourceEvent::SendRtpArgs::kTcpActive) {
        SockUtil::setNoDelay(_socket_rtp->rawFD(), false);
        _socket_rtp->setSendFlags(SOCKET_DEFAULE_FLAGS | FLAG_MORE);
    } else {
        SockUtil::setBroadcast(_socket_rtp->rawFD());
    }

    weak_ptr<PlaybackSVAC> weak_self = shared_from_this();
    _socket_rtp->setOnErr([weak_self](const SockException &err) {
        auto strong_self = weak_self.lock();
        if (strong_self) {
            strong_self->_is_connect = false;
            WarnL << "PlaybackSVAC socket error: " << err;
            strong_self->stop();
        }
    });

    InfoL << "PlaybackSVAC connected to " << _socket_rtp->get_peer_ip() << ":" << _socket_rtp->get_peer_port();

    // Start playback timer (10ms for raw RTP, 50ms for PS)
    float interval = _raw_mode ? 0.01f : 0.05f;
    _play_ticker.resetTime();
    _play_timer = make_shared<Timer>(interval, [weak_self]() {
        auto strong_self = weak_self.lock();
        if (!strong_self) return false;
        return strong_self->onTimer();
    }, _poller);
}

bool PlaybackSVAC::onTimer() {
    if (_stopped || !_is_connect) return false;

    if (_raw_mode) {
        if (_current_idx >= _rtp_index.size()) {
            InfoL << "PlaybackSVAC complete: " << _file_path;
            stop();
            return false;
        }

        int64_t elapsed = _play_ticker.elapsedTime();
        int64_t target = elapsed; // RTP index stamps are already relative ms

        while (_current_idx < _rtp_index.size() && _rtp_index[_current_idx].stamp_ms <= target) {
            feedNextRtp();
            _current_idx++;
        }
        return true;
    }

    if (_current_idx >= _index.size()) {
        // Playback complete
        InfoL << "PlaybackSVAC complete: " << _file_path;
        stop();
        return false;
    }

    int64_t elapsed = _play_ticker.elapsedTime();
    int64_t target_scr_ms = _base_scr_ms + elapsed;

    while (_current_idx < _index.size() && _index[_current_idx].scr_ms <= target_scr_ms) {
        feedNextChunk();
        _current_idx++;
    }
    return true;
}

void PlaybackSVAC::feedNextChunk() {
    if (!_file || _current_idx >= _index.size()) return;

    uint64_t offset = _index[_current_idx].offset;
    uint64_t next_offset = (_current_idx + 1 < _index.size()) ? _index[_current_idx + 1].offset : 0;

    // Get file size for the last chunk
    if (next_offset == 0) {
        fseek(_file.get(), 0, SEEK_END);
        next_offset = ftell(_file.get());
    }

    size_t size = (size_t)(next_offset - offset);
    if (size == 0 || size > 64 * 1024) return; // Sanity check

    auto buffer = make_shared<BufferLikeString>();
    buffer->resize(size);
    fseek(_file.get(), (long)offset, SEEK_SET);
    fread((void *)buffer->data(), 1, size, _file.get());

    uint64_t dts = (uint64_t)_index[_current_idx].scr_ms;
    auto frame = make_shared<FrameFromPtr>(CodecPS, buffer->data(), buffer->size(), dts, dts);
    // Hold buffer reference so data remains valid
    auto frame_cached = Frame::getCacheAbleFrame(frame);

    _rtp_encoder->inputFrame(frame_cached);
}

void PlaybackSVAC::feedNextRtp() {
    if (!_file || _current_idx >= _rtp_index.size()) return;

    auto &entry = _rtp_index[_current_idx];
    size_t total = entry.size;

    // Read raw RTP packet from dump (already positioned correctly from index)
    // Build buffer: [4 bytes TCP header reserved] [12-byte RTP header] [payload]
    BufferLikeString rtp_buf;
    rtp_buf = std::string(RtpPacket::kRtpTcpHeaderSize + total, '\0');
    auto ptr = (uint8_t *)rtp_buf.data();
    ptr[0] = '$';
    ptr[1] = 0; // interleaved channel
    ptr[2] = (uint8_t)(total >> 8);
    ptr[3] = (uint8_t)(total & 0xFF);

    fseek(_file.get(), (long)entry.offset, SEEK_SET);
    fread(ptr + RtpPacket::kRtpTcpHeaderSize, 1, total, _file.get());

    auto rtp_list = make_shared<List<Buffer::Ptr>>();
    rtp_list->emplace_back(std::make_shared<BufferLikeString>(std::move(rtp_buf)));
    onFlushRtpList(std::move(rtp_list));
}

void PlaybackSVAC::onFlushRtpList(shared_ptr<List<Buffer::Ptr>> rtp_list) {
    if (!_is_connect) return;

    size_t i = 0;
    auto size = rtp_list->size();
    rtp_list->for_each([&](Buffer::Ptr &packet) {
        try {
            switch (_args.con_type) {
                case MediaSourceEvent::SendRtpArgs::kUdpActive:
                    InfoL << "send packet size=" << packet->size() << " offset=" << (int)RtpPacket::kRtpTcpHeaderSize;
                    _socket_rtp->send(make_shared<BufferRtp>(std::move(packet), RtpPacket::kRtpTcpHeaderSize), nullptr, 0, ++i == size);
                    break;
                case MediaSourceEvent::SendRtpArgs::kTcpActive:
                    _socket_rtp->send(make_shared<BufferRtp>(std::move(packet), 2), nullptr, 0, ++i == size);
                    break;
                default: break;
            }
        } catch (std::exception &ex) {
            ErrorL << "PlaybackSVAC send error: " << ex.what();
        }
    });
}

void PlaybackSVAC::stop() {
    if (_stopped) return;
    _stopped = true;
    _is_connect = false;

    _play_timer.reset();
    _rtp_encoder.reset();
    if (_file) _file.reset();
    if (_socket_rtp) {
        _socket_rtp->closeSock();
    }
    _index.clear();
    _rtp_index.clear();

    if (_on_close) {
        _on_close(SockException(Err_shutdown, "playback stopped"));
    }
    InfoL << "PlaybackSVAC stopped: " << _file_path;
}

// Scan .mpeg file and build PS pack index
void PlaybackSVAC::indexFile() {
    _index.clear();
    fseek(_file.get(), 0, SEEK_END);
    long file_size = ftell(_file.get());
    fseek(_file.get(), 0, SEEK_SET);

    vector<uint8_t> buf(64 * 1024);
    size_t buf_pos = 0;
    size_t buf_len = 0;
    uint64_t file_offset = 0;

    auto fillBuf = [&]() -> bool {
        if (buf_pos > 32 * 1024 && buf_pos < buf_len) {
            memmove(buf.data(), buf.data() + buf_pos, buf_len - buf_pos);
            buf_len -= buf_pos;
            buf_pos = 0;
        }
        if (buf_len < 8) {
            size_t to_read = min(buf.size() - buf_len, (size_t)(file_size - file_offset));
            if (to_read == 0) return false;
            size_t n = fread(buf.data() + buf_len, 1, to_read, _file.get());
            if (n == 0) return false;
            buf_len += n;
            file_offset += n;
        }
        return buf_len - buf_pos >= 4;
    };

    while (fillBuf()) {
        if (buf[buf_pos] == 0x00 && buf[buf_pos+1] == 0x00 && buf[buf_pos+2] == 0x01 && buf[buf_pos+3] == 0xBA) {
            // Found PS pack header
            uint64_t pack_offset = file_offset - buf_len + buf_pos;
            if (buf_len - buf_pos >= 14) {
                int64_t scr = extractSCR(buf.data() + buf_pos + 4);
                PackEntry entry;
                entry.offset = pack_offset;
                entry.scr_ms = scr / 90; // 90kHz to ms
                _index.push_back(entry);
            }
            buf_pos += 4;
        } else {
            buf_pos++;
        }
    }

    InfoL << "PlaybackSVAC indexed " << _index.size() << " packs, file: " << _file_path;
}

void PlaybackSVAC::indexRtpFile() {
    _rtp_index.clear();
    fseek(_file.get(), 0, SEEK_END);
    long file_size = ftell(_file.get());
    fseek(_file.get(), 0, SEEK_SET);

    uint64_t file_offset = 0;
    uint32_t first_stamp = 0;
    bool first_stamp_set = false;
    uint32_t prev_stamp = 0;
    int64_t wrap_offset = 0; // Handle 32-bit timestamp wrap-around

    while (file_offset < (uint64_t)file_size) {
        uint8_t size_buf[2];
        if (fread(size_buf, 1, 2, _file.get()) != 2) break;
        uint16_t pkt_size = ((uint16_t)size_buf[0] << 8) | size_buf[1];
        if (pkt_size < RtpPacket::kRtpHeaderSize) break;

        uint64_t pkt_offset = file_offset + 2; // Offset of raw RTP data
        file_offset += 2 + pkt_size;

        // Read RTP header to get timestamp
        uint8_t rtp_hdr[12];
        if (fread(rtp_hdr, 1, 12, _file.get()) != 12) break;

        uint32_t stamp = ((uint32_t)rtp_hdr[4] << 24) | ((uint32_t)rtp_hdr[5] << 16)
                       | ((uint32_t)rtp_hdr[6] << 8) | rtp_hdr[7];

        if (!first_stamp_set) {
            first_stamp = stamp;
            first_stamp_set = true;
            prev_stamp = stamp;
        }

        // Detect timestamp wrap-around (32-bit RTP timestamp)
        // prev_stamp > stamp means timestamp decreased; if the drop is large (>2^31),
        // it's a wrap-around rather than out-of-order
        if (prev_stamp > stamp && (prev_stamp - stamp) > 0x80000000u) {
            wrap_offset += 0x100000000LL;
        }
        prev_stamp = stamp;

        int64_t abs_stamp = (int64_t)stamp + wrap_offset;
        int64_t stamp_ms = (abs_stamp - (int64_t)first_stamp) / 90; // 90kHz -> ms

        RtpEntry entry;
        entry.offset = pkt_offset;
        entry.size = pkt_size;
        entry.stamp_ms = stamp_ms;
        _rtp_index.push_back(entry);

        // Seek to start of next record
        fseek(_file.get(), (long)(file_offset), SEEK_SET);
    }

    InfoL << "PlaybackSVAC indexed " << _rtp_index.size() << " RTP packets, file: " << _file_path;
    if (!_rtp_index.empty()) {
        InfoL << "First stamp_ms=" << _rtp_index[0].stamp_ms
              << ", last stamp_ms=" << _rtp_index.back().stamp_ms
              << ", duration=" << (_rtp_index.back().stamp_ms - _rtp_index[0].stamp_ms) << "ms";
    }
}

// Extract 33-bit SCR_base from PS pack header bytes after 00 00 01 BA
int64_t PlaybackSVAC::extractSCR(const uint8_t *b) {
    // MPEG-2 pack header SCR extraction per mpeg-pack-header.c
    // b[0..5] are the 6 bytes after the start code
    int64_t scr_base = ((int64_t)(b[0] >> 3) & 0x07) << 30 |
                       ((int64_t)(b[0] & 0x03) << 28) |
                       ((int64_t)b[1] << 20) |
                       ((int64_t)(b[2] >> 3) & 0x1F) << 15 |
                       ((int64_t)(b[2] & 0x03) << 13) |
                       ((int64_t)b[3] << 5) |
                       ((int64_t)(b[4] >> 3) & 0x1F);
    return scr_base;
}

} // namespace mediakit
#endif // defined(ENABLE_RTPPROXY)
