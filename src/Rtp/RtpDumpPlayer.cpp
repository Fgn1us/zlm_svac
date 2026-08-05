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
#include <algorithm>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")
#endif

using namespace std;
using namespace toolkit;

namespace mediakit {

// RTP 视频时钟频率 (90000 Hz)
static constexpr uint32_t kVideoSampleRate = 90000;

RtpDumpPlayer::RtpDumpPlayer() {}

RtpDumpPlayer::~RtpDumpPlayer() {
    // 析构时不存在并发，直接清理（不能使用 shared_from_this）
    stopInPoller();
}

void RtpDumpPlayer::start(const PlayArgs &args, onCompleteCB on_complete, onErrorCB on_error) {
#ifdef _WIN32
    // 提升 Windows 定时器精度到 1ms：
    // 默认系统定时器分辨率仅 15.6ms，会导致 doDelayTask / 轮询触发系统性偏晚，
    // 发送节奏落后于目标倍速（2 倍速实测只到约 1.5 倍）。程序生命周期内提升一次。
    static bool s_high_res_timer = ([]() {
        timeBeginPeriod(1);
        return true;
    })();
    (void)s_high_res_timer;
#endif
    _args = args;
    _on_complete = std::move(on_complete);
    _on_error = std::move(on_error);
    _identifier = args.file_path;

    // 获取 poller
    _poller = EventPollerPool::Instance().getPoller();

    // 创建 UDP socket 并提前绑定本地端口（在 DNS 解析之前完成，使 getLocalPort() 立即可用）
    _socket = Socket::createSocket(_poller, false);

    string ifr_ip = "0.0.0.0";
    if (_args.src_port) {
        if (!_socket->bindUdpSock(_args.src_port, ifr_ip, true)) {
            notifyError(SockException(Err_other, "bind udp sock failed on port: " + to_string(_args.src_port)));
            return;
        }
    } else {
        auto pr = std::make_pair(_socket, Socket::createSocket(_poller, false));
        makeSockPair(pr, ifr_ip, true, true);
    }

    // 加大发送缓冲区
    SockUtil::setSendBuf(_socket->rawFD(), 4 * 1024 * 1024);

    // 设置错误回调
    auto ws = weak_ptr<RtpDumpPlayer>(shared_from_this());
    _socket->setOnErr([ws](const SockException &err) {
        auto s = ws.lock();
        if (s) {
            s->notifyError(err);
        }
    });

    // DNS 解析放在后台线程（端口已绑定，不影响 local_port 获取）
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
            // 绑定目标地址
            strong_self->_socket->bindPeerAddr((struct sockaddr *)&addr, 0, true);

            // 解析 dump 文件
            if (!strong_self->parseDumpFile(strong_self->_args.file_path)) {
                // parseDumpFile 内部已调用 notifyError
                return;
            }

            if (strong_self->_packets.empty()) {
                strong_self->notifyError(SockException(Err_other, "dump file is empty or contains no valid RTP packets"));
                return;
            }

            // 开始发送（启动参考时钟，避免定时器漂移累积）
            strong_self->_playing = true;
            strong_self->_current_index = 0;
            // 重置变速锚点，保证从头播放时时间戳重写以文件起点为基准
            strong_self->_speed_base_offset_ms = 0;
            strong_self->_speed_base_elapsed_ms = 0;
            strong_self->_last_speed_log_elapsed_ms = 0;
            strong_self->_last_speed_log_pos_ms = 0;
            strong_self->_start_ticker.resetTime();
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
    if (_poller && !_poller->isCurrentThread()) {
        // API 线程调用，串行化到 poller 线程执行，避免与发送循环竞争
        auto weak_self = weak_ptr<RtpDumpPlayer>(shared_from_this());
        _poller->sync([weak_self]() {
            auto strong_self = weak_self.lock();
            if (strong_self) {
                strong_self->stopInPoller();
            }
        });
        return;
    }
    stopInPoller();
}

void RtpDumpPlayer::stopInPoller() {
    _playing = false;
    _paused = false;
    _paused_duration_ms = 0;
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

bool RtpDumpPlayer::isPaused() const {
    return _paused;
}

void RtpDumpPlayer::pause() {
    if (!_playing || _paused) {
        return;
    }
    // 串行化到 poller 线程执行，避免与发送循环竞争共享状态
    auto weak_self = weak_ptr<RtpDumpPlayer>(shared_from_this());
    _poller->sync([weak_self]() {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }
        strong_self->_paused = true;
        strong_self->_pause_begin_ms = strong_self->_start_ticker.elapsedTime();
        if (strong_self->_delay_task) {
            strong_self->_delay_task->cancel();
            strong_self->_delay_task = nullptr;
        }
        InfoL << "[RtpDumpPlayer] paused: " << strong_self->_identifier
              << ", index=" << strong_self->_current_index << "/" << strong_self->_total_packets;
    });
}

void RtpDumpPlayer::resume() {
    if (!_playing || !_paused) {
        return;
    }
    auto weak_self = weak_ptr<RtpDumpPlayer>(shared_from_this());
    _poller->sync([weak_self]() {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }
        strong_self->_paused = false;
        // 累加暂停时长，使参考时钟在暂停期间不推进
        strong_self->_paused_duration_ms += (strong_self->_start_ticker.elapsedTime() - strong_self->_pause_begin_ms);
        InfoL << "[RtpDumpPlayer] resumed: " << strong_self->_identifier
              << ", paused_duration=" << strong_self->_paused_duration_ms << "ms";
        strong_self->sendNextPacket();
    });
}

float RtpDumpPlayer::getSpeed() const {
    return _args.speed;
}

void RtpDumpPlayer::setSpeed(float speed) {
    if (!_playing || speed <= 0) {
        return;
    }
    // 串行化到 poller 线程执行，避免与发送循环竞争共享状态
    auto weak_self = weak_ptr<RtpDumpPlayer>(shared_from_this());
    _poller->sync([weak_self, speed]() {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }
        // 取消当前定时器
        if (strong_self->_delay_task) {
            strong_self->_delay_task->cancel();
            strong_self->_delay_task = nullptr;
        }

        // 记录变速锚点：当前未发送位置和当前有效耗时
        // 当前暂停中的时间尚未累加到 _paused_duration_ms，需要临时扣除
        uint64_t current_pause = strong_self->_paused ? (strong_self->_start_ticker.elapsedTime() - strong_self->_pause_begin_ms) : 0;
        strong_self->_speed_base_elapsed_ms = strong_self->_start_ticker.elapsedTime() - strong_self->_paused_duration_ms - current_pause;
        strong_self->_speed_base_offset_ms = (strong_self->_current_index < strong_self->_packets.size()) ? strong_self->_packets[strong_self->_current_index].offset_ms : strong_self->_total_duration_ms;
        strong_self->_args.speed = speed;

        InfoL << "[RtpDumpPlayer] speed changed: " << strong_self->_identifier
              << ", speed=" << speed
              << ", base_offset=" << strong_self->_speed_base_offset_ms << "ms"
              << ", base_elapsed=" << strong_self->_speed_base_elapsed_ms << "ms";

        // 非暂停状态下立即继续发送
        if (!strong_self->_paused) {
            strong_self->sendNextPacket();
        }
    });
}

uint64_t RtpDumpPlayer::getCurrentOffsetMs() const {
    if (_packets.empty()) return 0;
    if (_current_index >= _packets.size()) return _total_duration_ms;
    return _packets[_current_index].offset_ms;
}

void RtpDumpPlayer::seek(int64_t offset_sec) {
    if (!_playing || _packets.empty()) {
        return;
    }

    // 串行化到 poller 线程执行（同步等待），保证 seek 返回后位置已生效
    auto weak_self = weak_ptr<RtpDumpPlayer>(shared_from_this());
    _poller->sync([weak_self, offset_sec]() {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }

        // 计算目标偏移（毫秒）
        uint64_t current_ms = strong_self->getCurrentOffsetMs();
        int64_t target_ms = (int64_t)current_ms + offset_sec * 1000;
        if (target_ms < 0) target_ms = 0;
        if ((uint64_t)target_ms > strong_self->_total_duration_ms) target_ms = (int64_t)strong_self->_total_duration_ms;

        // 二分查找第一个 offset_ms >= target_ms 的包（帧对齐）
        auto it = std::lower_bound(strong_self->_packets.begin(), strong_self->_packets.end(), (uint64_t)target_ms,
            [](const ParsedPacket &pkt, uint64_t target) { return pkt.offset_ms < target; });
        strong_self->_current_index = std::distance(strong_self->_packets.begin(), it);
        if (strong_self->_current_index >= strong_self->_packets.size()) {
            strong_self->_current_index = strong_self->_packets.size() - 1;
        }

        // 重设变速锚点为当前位置
        uint64_t current_pause = strong_self->_paused ? (strong_self->_start_ticker.elapsedTime() - strong_self->_pause_begin_ms) : 0;
        strong_self->_speed_base_elapsed_ms = strong_self->_start_ticker.elapsedTime() - strong_self->_paused_duration_ms - current_pause;
        strong_self->_speed_base_offset_ms = strong_self->_packets[strong_self->_current_index].offset_ms;

        // 取消当前定时器
        if (strong_self->_delay_task) {
            strong_self->_delay_task->cancel();
            strong_self->_delay_task = nullptr;
        }

        InfoL << "[RtpDumpPlayer] seek: " << strong_self->_identifier
              << ", offset_sec=" << offset_sec
              << ", new_offset=" << strong_self->_speed_base_offset_ms << "ms"
              << ", index=" << strong_self->_current_index << "/" << strong_self->_total_packets;

        // 非暂停状态下立即继续发送
        if (!strong_self->_paused) {
            strong_self->sendNextPacket();
        }
    });
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
    _first_raw_ts = _packets[0].raw_timestamp;

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

// 轮询间隔（毫秒），快速轮询避免 doDelayTask 调度开销累积
// 接近目标时刻时改用精确计时，兼顾精度与抗漂移
static constexpr uint64_t kPollIntervalMs = 5;

void RtpDumpPlayer::sendNextPacket() {
    if (!_playing || _current_index >= _packets.size()) {
        onPlayCompleted();
        return;
    }

    if (_paused) {
        return;
    }

    // 每 5s 打印一次发送倍速，便于验证发送节奏是否达到目标倍速
    // avg_speed：从播放开始算的累计平均（变速后永远低于目标，仅作参考）
    // inst_speed：本日志区间内的瞬时倍速，应逼近 target（变速后每次 5s 墙钟推进 10s 内容 = 2x）
    uint64_t elapsed_ms = _start_ticker.elapsedTime();
    if (elapsed_ms - _last_speed_log_elapsed_ms >= 5000) {
        uint64_t pos_ms = _current_index < _packets.size() ? _packets[_current_index].offset_ms : _total_duration_ms;
        double avg_speed = elapsed_ms ? (double)pos_ms / (double)elapsed_ms : 0.0;
        uint64_t d_elapsed = elapsed_ms - _last_speed_log_elapsed_ms;
        uint64_t d_pos = pos_ms - _last_speed_log_pos_ms;
        double inst_speed = d_elapsed ? (double)d_pos / (double)d_elapsed : 0.0;
        _last_speed_log_elapsed_ms = elapsed_ms;
        _last_speed_log_pos_ms = pos_ms;
        InfoL << "[RtpDumpPlayer] avg_speed=" << avg_speed
              << "x, inst_speed=" << inst_speed
              << "x (target=" << _args.speed << "x), pos=" << (pos_ms / 1000.0)
              << "s, elapsed=" << elapsed_ms << "ms";
    }

    // 发送所有已到时间的包，用循环防止追帧时递归过深
    for (;;) {
        uint64_t offset = _packets[_current_index].offset_ms;
        uint64_t target_elapsed = _speed_base_elapsed_ms + (uint64_t)((double)(offset - _speed_base_offset_ms) / _args.speed);
        uint64_t effective_elapsed = _start_ticker.elapsedTime() - _paused_duration_ms;

        if (target_elapsed > effective_elapsed) {
            // 还没到目标时刻，需要等待
            int64_t remain_ms = (int64_t)(target_elapsed - effective_elapsed);

            if ((uint64_t)remain_ms > kPollIntervalMs) {
                // 距离目标还远 → 快速轮询（避免 doDelayTask 开销累积到高速倍速）
                break;
            }
            // 距离目标很近（≤5ms）→ 精确计时，保证倍速精度
            auto weak_self = weak_ptr<RtpDumpPlayer>(shared_from_this());
            _delay_task = _poller->doDelayTask((uint64_t)remain_ms, [weak_self]() -> uint64_t {
                auto strong_self = weak_self.lock();
                if (strong_self) {
                    strong_self->sendNextPacket();
                }
                return 0;
            });
            return;
        }

        // 已到时间，立即发送当前这批（同一时间戳的所有包）
        uint64_t current_offset = _packets[_current_index].offset_ms;
        while (_current_index < _packets.size() && _packets[_current_index].offset_ms == current_offset) {
            auto &pkt = _packets[_current_index];
            // 基于变速锚点重写 RTP 时间戳：
            // - 变速瞬间时间戳保持连续，避免接收端因时间戳回退而丢帧/卡顿
            // - 变速后时间戳推进与发送墙钟完全匹配，按时间戳驱动的接收端即为目标倍速
            // (从头播放时锚点为 0，等价于原始时间戳；变速到 1.0 倍速时也能保持连续)
            auto *header = (RtpHeader *)pkt.data->data();
            double wall_sec = (double)_speed_base_elapsed_ms / 1000.0
                            + (double)(pkt.offset_ms - _speed_base_offset_ms) / 1000.0 / _args.speed;
            uint32_t scaled_stamp = _first_raw_ts + (uint32_t)std::llround(wall_sec * kVideoSampleRate);
            header->stamp = htonl(scaled_stamp);
            _socket->send(pkt.data, nullptr, 0, true);
            _sent_packets++;
            _sent_bytes += pkt.data->size();
            _current_index++;
        }

        if (_current_index >= _packets.size()) {
            onPlayCompleted();
            return;
        }
    }

    // 快速轮询：固定 5ms 后再检查
    auto weak_self = weak_ptr<RtpDumpPlayer>(shared_from_this());
    _delay_task = _poller->doDelayTask(kPollIntervalMs, [weak_self]() -> uint64_t {
        auto strong_self = weak_self.lock();
        if (strong_self) {
            strong_self->sendNextPacket();
        }
        return 0;
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
