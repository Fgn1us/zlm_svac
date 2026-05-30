/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#if defined(ENABLE_RTPPROXY)

#include "RtpDumpPlayer.h"
#include "Rtsp/Rtsp.h"
#include "Util/File.h"
#include "Util/logger.h"
#include "Util/util.h"
#include "Poller/EventPoller.h"
#include "Poller/Timer.h"
#include "Thread/WorkThreadPool.h"
#include "Network/Socket.h"

using namespace std;
using namespace toolkit;

namespace mediakit {

// RTP 视频时钟频率 (90000 Hz)
static constexpr uint32_t kVideoSampleRate = 90000;

RtpDumpPlayer::RtpDumpPlayer() {}

RtpDumpPlayer::~RtpDumpPlayer() {
    stop();
}

void RtpDumpPlayer::start(const PlayArgs &args, onCompleteCB on_complete, onErrorCB on_error) {
    _args = args;
    _on_complete = std::move(on_complete);
    _on_error = std::move(on_error);
    _identifier = args.file_path;

    // 获取 poller
    _poller = EventPollerPool::Instance().getPoller();

    // 创建 UDP socket
    _socket = Socket::createSocket(_poller, false);

    // DNS 解析放在后台线程
    weak_ptr<RtpDumpPlayer> weak_self = shared_from_this();
    WorkThreadPool::Instance().getPoller()->async([args, weak_self]() {
        struct sockaddr_storage addr;
        if (!SockUtil::getDomainIP(args.dst_url.data(), args.dst_port, addr, AF_INET, SOCK_DGRAM, IPPROTO_UDP)) {
            auto strong_self = weak_self.lock();
            if (strong_self) {
                strong_self->_poller->async([strong_self]() {
                    strong_self->notifyError(SockException(Err_dns, "dns resolution failed: " + strong_self->_args.dst_url));
                });
            }
            return;
        }

        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }

        strong_self->_poller->async([strong_self, addr]() {
            string ifr_ip = addr.ss_family == AF_INET ? "0.0.0.0" : "::";

            // 绑定本地端口
            if (strong_self->_args.src_port) {
                if (!strong_self->_socket->bindUdpSock(strong_self->_args.src_port, ifr_ip, true)) {
                    strong_self->notifyError(SockException(Err_other, "bind udp sock failed on port: " + to_string(strong_self->_args.src_port)));
                    return;
                }
            } else {
                auto pr = std::make_pair(strong_self->_socket, Socket::createSocket(strong_self->_poller, false));
                makeSockPair(pr, ifr_ip, true, true);
            }

            // 绑定目标地址
            strong_self->_socket->bindPeerAddr((struct sockaddr *)&addr, 0, true);

            // 加大发送缓冲区
            SockUtil::setSendBuf(strong_self->_socket->rawFD(), 4 * 1024 * 1024);

            // 设置错误回调
            auto ws = weak_ptr<RtpDumpPlayer>(strong_self);
            strong_self->_socket->setOnErr([ws](const SockException &err) {
                auto s = ws.lock();
                if (s) {
                    s->notifyError(err);
                }
            });

            // 解析 dump 文件
            if (!strong_self->parseDumpFile(strong_self->_args.file_path)) {
                // parseDumpFile 内部已调用 notifyError
                return;
            }

            if (strong_self->_packets.empty()) {
                strong_self->notifyError(SockException(Err_other, "dump file is empty or contains no valid RTP packets"));
                return;
            }

            // 开始发送
            strong_self->_playing = true;
            strong_self->_current_index = 0;
            InfoL << "[RtpDumpPlayer] start playback: " << strong_self->_args.file_path
                  << " -> " << strong_self->_args.dst_url << ":" << strong_self->_args.dst_port
                  << ", packets=" << strong_self->_total_packets
                  << ", duration_ms=" << strong_self->_total_duration_ms
                  << ", local_port=" << strong_self->_socket->get_local_port();

            strong_self->sendNextPacket();
        });
    });
}

void RtpDumpPlayer::stop() {
    _playing = false;
    if (_delay_task) {
        _delay_task->cancel();
        _delay_task = nullptr;
    }
    if (_socket) {
        _socket->closeSock();
        _socket = nullptr;
    }
    InfoL << "[RtpDumpPlayer] stopped: " << _identifier;
}

bool RtpDumpPlayer::isPlaying() const {
    return _playing;
}

uint64_t RtpDumpPlayer::getSentPackets() const {
    return _sent_packets;
}

uint64_t RtpDumpPlayer::getSentBytes() const {
    return _sent_bytes;
}

uint64_t RtpDumpPlayer::getDurationMs() const {
    return _total_duration_ms;
}

uint64_t RtpDumpPlayer::getTotalPackets() const {
    return _total_packets;
}

uint16_t RtpDumpPlayer::getLocalPort() const {
    return _socket ? _socket->get_local_port() : 0;
}

bool RtpDumpPlayer::parseDumpFile(const std::string &path) {
    // 读取整个文件
    auto file_content = File::loadFile(path.data());
    if (file_content.empty()) {
        notifyError(SockException(Err_other, "failed to read dump file: " + path));
        return false;
    }

    const char *ptr = file_content.data();
    const char *end = ptr + file_content.size();
    size_t invalid_packets = 0;

    // 第一遍：解析所有 RTP 包
    while (ptr + 2 <= end) {
        // 读取 2 字节大端长度
        uint16_t len = ntohs(*(uint16_t *)ptr);
        ptr += 2;

        if (len == 0 || ptr + len > end) {
            // 无效长度，跳过
            WarnL << "[RtpDumpPlayer] invalid packet length: " << len << " at offset " << (ptr - file_content.data() - 2);
            invalid_packets++;
            break;
        }

        // 校验最小 RTP 头 (12 字节)
        if (len < RtpPacket::kRtpHeaderSize) {
            WarnL << "[RtpDumpPlayer] packet too small: " << len << " bytes, skipping";
            invalid_packets++;
            ptr += len;
            continue;
        }

        // 提取 RTP 时间戳（网络序 → 主机序）
        const RtpHeader *header = (const RtpHeader *)ptr;
        uint32_t stamp = ntohl(header->stamp);

        // 拷贝 RTP 数据
        auto buf = BufferRaw::create();
        buf->assign(ptr, len);

        _packets.push_back({stamp, 0, std::move(buf)});
        ptr += len;
    }

    _total_packets = _packets.size();

    if (_total_packets == 0) {
        return false;
    }

    if (invalid_packets > 0) {
        WarnL << "[RtpDumpPlayer] skipped " << invalid_packets << " invalid packets in " << path;
    }

    // 第二遍：计算累计时间偏移
    _packets[0].offset_ms = 0;
    for (size_t i = 1; i < _packets.size(); ++i) {
        int64_t delta_ts = (int64_t)_packets[i].raw_timestamp - (int64_t)_packets[i - 1].raw_timestamp;

        // 处理 32 位时间戳回绕
        if (delta_ts < -(int64_t)(0x7FFFFFFF)) {
            // 回绕：当前值远小于上一值，说明发生了回绕
            delta_ts += (int64_t)0xFFFFFFFF + 1;
        } else if (delta_ts > (int64_t)0x7FFFFFFF) {
            // 反向回绕（几乎不会发生，但防御性处理）
            delta_ts -= (int64_t)0xFFFFFFFF + 1;
        }

        // 90kHz 时钟 → 毫秒
        uint64_t delta_ms = (uint64_t)((delta_ts * 1000) / kVideoSampleRate);
        _packets[i].offset_ms = _packets[i - 1].offset_ms + delta_ms;
    }

    _total_duration_ms = _packets.back().offset_ms;

    InfoL << "[RtpDumpPlayer] parsed " << _total_packets << " packets from " << path
          << ", total_duration=" << _total_duration_ms << "ms"
          << " (" << (_total_duration_ms / 1000) << "s)";

    return true;
}

void RtpDumpPlayer::sendNextPacket() {
    if (!_playing || _current_index >= _packets.size()) {
        onPlayCompleted();
        return;
    }

    auto &pkt = _packets[_current_index];

    // 发送当前包
    _socket->send(pkt.data, nullptr, 0, true);
    _sent_packets++;
    _sent_bytes += pkt.data->size();
    _current_index++;

    if (_current_index >= _packets.size()) {
        onPlayCompleted();
        return;
    }

    // 计算到下一个包的时间间隔
    uint64_t this_offset = pkt.offset_ms;
    uint64_t next_offset = _packets[_current_index].offset_ms;
    uint64_t interval_ms;
    if (next_offset > this_offset) {
        interval_ms = (uint64_t)((double)(next_offset - this_offset) / _args.speed);
    } else {
        // 时间戳相同或回退，立即发送下一包
        interval_ms = 0;
    }

    // 通过 doDelayTask 调度下一次发送
    auto weak_self = weak_ptr<RtpDumpPlayer>(shared_from_this());
    _delay_task = _poller->doDelayTask(interval_ms, [weak_self]() -> uint64_t {
        auto strong_self = weak_self.lock();
        if (strong_self) {
            strong_self->sendNextPacket();
        }
        return 0; // 不重复
    });
}

void RtpDumpPlayer::onPlayCompleted() {
    _playing = false;
    if (_delay_task) {
        _delay_task->cancel();
        _delay_task = nullptr;
    }
    if (_socket) {
        _socket->closeSock();
    }
    InfoL << "[RtpDumpPlayer] playback completed: " << _identifier
          << ", sent_packets=" << _sent_packets << ", sent_bytes=" << _sent_bytes;

    if (_on_complete) {
        _on_complete();
    }
}

void RtpDumpPlayer::notifyError(const toolkit::SockException &ex) {
    _playing = false;
    if (_delay_task) {
        _delay_task->cancel();
        _delay_task = nullptr;
    }
    if (_socket) {
        _socket->closeSock();
    }
    WarnL << "[RtpDumpPlayer] error: " << _identifier << " - " << ex.what();

    if (_on_error) {
        _on_error(ex);
    }
}

} // namespace mediakit
#endif // defined(ENABLE_RTPPROXY)
