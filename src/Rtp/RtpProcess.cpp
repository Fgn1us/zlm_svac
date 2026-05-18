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
#include <chrono>
#include <iomanip>
#include "GB28181Process.h"
#include "GB35114Process.h"
#include "RtpProcess.h"
#include "Util/File.h"
#include "Common/config.h"

using namespace std;
using namespace toolkit;

// 在创建_muxer对象前(也就是推流鉴权成功前)，需要先缓存frame，这样可以防止丢包，提高体验  [AUTO-TRANSLATED:fb12a6c2]
// Before creating the _muxer object (before the streaming authentication is successful), you need to cache the frame first, which can prevent packet loss and improve the experience.
// 但是同时需要控制缓冲长度，防止内存溢出。最多缓存10秒数据，应该足矣等待鉴权hook返回  [AUTO-TRANSLATED:23ff0a4a]
// But at the same time, you need to control the buffer length to prevent memory overflow. Caching 10 seconds of data should be enough to wait for the authentication hook to return.
static constexpr size_t kMaxCachedFrameMS = 10 * 1000;

namespace mediakit {

RtpProcess::Ptr RtpProcess::createProcess(const MediaTuple &tuple) {
    RtpProcess::Ptr ret(new RtpProcess(tuple));
    ret->createTimer();
    return ret;
}

RtpProcess::RtpProcess(const MediaTuple &tuple) {
    _media_info.schema = "rtp";
    static_cast<MediaTuple &>(_media_info) = tuple;

    // 自动开始 dump（如果配置了 dumpDir）
    GET_CONFIG(string, dump_dir, RtpProxy::kDumpDir);
    if (!dump_dir.empty()) {
        startAutoDump(dump_dir);
    }
}

void RtpProcess::flush() {
    if (_process) {
        _process->flush();
    }
}

RtpProcess::~RtpProcess() {
    uint64_t duration = (_last_frame_time.createdTime() - _last_frame_time.elapsedTime()) / 1000;
    WarnP(this) << "RTP推流器("
                << _media_info.shortUrl()
                << ")断开,耗时(s):" << duration;

    // 流量统计事件广播  [AUTO-TRANSLATED:6b0b1234]
    // Traffic statistics event broadcast
    GET_CONFIG(uint32_t, iFlowThreshold, General::kFlowThreshold);
    if (_total_bytes >= iFlowThreshold * 1024) {
        try {
            NOTICE_EMIT(BroadcastFlowReportArgs, Broadcast::kBroadcastFlowReport, _media_info, _total_bytes, duration, false, *this);
        } catch (std::exception &ex) {
            WarnL << "Exception occurred: " << ex.what();
        }
    }
}

void RtpProcess::onManager() {
    if (!alive()) {
        onDetach(SockException(Err_timeout, "RtpProcess timeout"));
    }
    checkDumpRotate();
}

void RtpProcess::createTimer() {
    // 创建超时管理定时器  [AUTO-TRANSLATED:865cf865]
    // Create a timeout management timer
    weak_ptr<RtpProcess> weakSelf = shared_from_this();
    _timer = std::make_shared<Timer>(3.0f, [weakSelf] {
        auto strongSelf = weakSelf.lock();
        if (!strongSelf) {
            return false;
        }
        strongSelf->onManager();
        return true;
    }, EventPollerPool::Instance().getPoller());
}

bool RtpProcess::inputRtp(bool is_udp, const Socket::Ptr &sock, const char *data, size_t len, const struct sockaddr *addr, uint64_t *dts_out) {
    if (!isRtp(data, len)) {
        WarnP(this) << "Not rtp packet";
        return false;
    }
    if (!_auth_err.empty()) {
        throw toolkit::SockException(toolkit::Err_other, _auth_err);
    }
    auto header = (RtpHeader *) data;
    if (_sock != sock) {
        // 第一次运行本函数  [AUTO-TRANSLATED:a1d7ac17]
        // First time running this function
        bool first = !_sock;
        _sock = sock;
        _addr.reset(new sockaddr_storage(*((sockaddr_storage *)addr)));
        if (first) {
            emitOnPublish(ntohl(header->ssrc));
            _cache_ticker.resetTime();
        }
    }

    _total_bytes += len;
    if (_auto_enabled && _save_file_rtp_auto) {
        uint16_t size = (uint16_t)len;
        size = htons(size);
        fwrite((uint8_t *) &size, 2, 1, _save_file_rtp_auto.get());
        fwrite((uint8_t *) data, len, 1, _save_file_rtp_auto.get());
    }
    if (_manual_enabled && _save_file_rtp_manual) {
        uint16_t size = (uint16_t)len;
        size = htons(size);
        fwrite((uint8_t *) &size, 2, 1, _save_file_rtp_manual.get());
        fwrite((uint8_t *) data, len, 1, _save_file_rtp_manual.get());
    }
    if (!_process) {
        _media_info.protocol = is_udp ? "udp" : "tcp";
        _process = std::make_shared<GB35114Process>(_media_info, this);
    }

    onRtp(ntohs(header->seq), ntohl(header->stamp), 0/*不发送sr,所以可以设置为0*/ , 90000/*ps/ts流时间戳按照90K采样率*/, len);

    GET_CONFIG(string, dump_dir, RtpProxy::kDumpDir);
    if (_muxer && !_muxer->isEnabled() && !dts_out && dump_dir.empty()) {
        // 无人访问、且不取时间戳、不导出调试文件时，我们可以直接丢弃数据  [AUTO-TRANSLATED:2fc75705]
        // When there is no access, and no timestamp is taken, and no debug file is exported, we can directly discard the data.
        _last_frame_time.resetTime();
        return false;
    }

    bool ret = _process ? _process->inputRtp(is_udp, data, len) : false;
    if (dts_out) {
        *dts_out = _dts;
    }
    return ret;
}

bool RtpProcess::inputFrame(const Frame::Ptr &frame) {
    _dts = frame->dts();
    if (_muxer) {
        _last_frame_time.resetTime();
        return _muxer->inputFrame(frame);
    }
    if (_cache_ticker.elapsedTime() > kMaxCachedFrameMS) {
        WarnL << "Cached frame of stream(" << _media_info.stream << ") is too much, your on_publish hook responded too late!";
        return false;
    }
    auto frame_cached = Frame::getCacheAbleFrame(frame);
    lock_guard<recursive_mutex> lck(_func_mtx);
    _cached_func.emplace_back([this, frame_cached]() {
        _last_frame_time.resetTime();
        _muxer->inputFrame(frame_cached);
    });
    return true;
}

bool RtpProcess::addTrack(const Track::Ptr &track) {
    if (_muxer) {
        return _muxer->addTrack(track);
    }

    lock_guard<recursive_mutex> lck(_func_mtx);
    _cached_func.emplace_back([this, track]() {
        _muxer->addTrack(track);
    });
    return true;
}

void RtpProcess::addTrackCompleted() {
    if (_muxer) {
        _muxer->addTrackCompleted();
    } else {
        lock_guard<recursive_mutex> lck(_func_mtx);
        _cached_func.emplace_back([this]() {
            _muxer->addTrackCompleted();
        });
    }
}

void RtpProcess::doCachedFunc() {
    lock_guard<recursive_mutex> lck(_func_mtx);
    for (auto &func : _cached_func) {
        func();
    }
    _cached_func.clear();
}

bool RtpProcess::alive() {
    if (_pause_timeout) {
        if (_last_check_alive.elapsedTime() < _pause_seconds * 1000) {
            return true;
        }
        // 最多暂停_pause_seconds秒的rtp超时检测，因为NAT映射有效期一般不会太长
        _pause_timeout = false;
    }

    _last_check_alive.resetTime();
    GET_CONFIG(uint64_t, timeoutSec, RtpProxy::kTimeoutSec)
    return _last_frame_time.elapsedTime() < timeoutSec * 1000;
}

void RtpProcess::pauseRtpTimeout(bool pause, uint32_t pause_seconds) {
    _pause_timeout = pause;
    // 默认5分钟恢复超时监测
    _pause_seconds = pause_seconds ? pause_seconds : 300;
    if (!pause) {
        _last_frame_time.resetTime();
    }
}

void RtpProcess::setOnlyTrack(OnlyTrack only_track) {
    _only_track = only_track;
}

void RtpProcess::onDetach(const SockException &ex) {
    if (_on_detach) {
        WarnL << ex << ", stream_id: " << getIdentifier();
        _on_detach(ex);
    }
}

void RtpProcess::setOnDetach(onDetachCB cb) {
    _on_detach = std::move(cb);
}

string RtpProcess::get_peer_ip() {
    try {
        return _addr ? SockUtil::inet_ntoa((sockaddr *)_addr.get()) : "::";
    } catch (std::exception &ex) {
        return "::";
    }
}

uint16_t RtpProcess::get_peer_port() {
    try {
        return _addr ? SockUtil::inet_port((sockaddr *)_addr.get()) : 0;
    } catch (std::exception &ex) {
        return 0;
    }
}

string RtpProcess::get_local_ip() {
    return _sock ? _sock->get_local_ip() : "::";
}

uint16_t RtpProcess::get_local_port() {
    return _sock ? _sock->get_local_port() : 0;
}

string RtpProcess::getIdentifier() const {
    return _media_info.stream;
}

void RtpProcess::emitOnPublish(uint32_t ssrc) {
    weak_ptr<RtpProcess> weak_self = shared_from_this();
    Broadcast::PublishAuthInvoker invoker = [weak_self, ssrc](const string &err, const ProtocolOption &option) {
        auto strong_self = weak_self.lock();
        if (!strong_self) {
            return;
        }
        auto poller = strong_self->getOwnerPoller(MediaSource::NullMediaSource());
        poller->async([weak_self, err, option, ssrc]() {
            auto strong_self = weak_self.lock();
            if (!strong_self) {
                return;
            }
            if (err.empty()) {
                strong_self->_muxer = std::make_shared<MultiMediaSourceMuxer>(strong_self->_media_info, 0.0f, option);
                switch (strong_self->_only_track) {
                    case kOnlyAudio: strong_self->_muxer->setOnlyAudio(); break;
                    case kOnlyVideo: strong_self->_muxer->enableAudio(false); break;
                    default: break;
                }
                strong_self->_muxer->setMediaListener(strong_self);
                strong_self->doCachedFunc();
                InfoP(strong_self) << "允许RTP推流，ssrc: " << printSSRC(ssrc);
            } else {
                strong_self->_auth_err = err;
                WarnP(strong_self) << "禁止RTP推流:" << err;
            }
        });
    };

    // 触发推流鉴权事件  [AUTO-TRANSLATED:cd889b29]
    // Trigger the streaming authentication event
    auto flag = NOTICE_EMIT(BroadcastMediaPublishArgs, Broadcast::kBroadcastMediaPublish, MediaOriginType::rtp_push, _media_info, invoker, *this);
    if (!flag) {
        // 该事件无人监听,默认不鉴权  [AUTO-TRANSLATED:e1fbc6ae]
        // No one is listening to this event, and authentication is not performed by default.
        invoker("", ProtocolOption());
    }
}

MediaOriginType RtpProcess::getOriginType(MediaSource &sender) const{
    return MediaOriginType::rtp_push;
}

string RtpProcess::getOriginUrl(MediaSource &sender) const {
    return _media_info.getUrl();
}

std::shared_ptr<SockInfo> RtpProcess::getOriginSock(MediaSource &sender) const {
    return const_cast<RtpProcess *>(this)->shared_from_this();
}

RtpProcess::Ptr RtpProcess::getRtpProcess(mediakit::MediaSource &sender) const {
    return const_cast<RtpProcess *>(this)->shared_from_this();
}

bool RtpProcess::close(mediakit::MediaSource &sender) {
    onDetach(SockException(Err_shutdown, "close media"));
    return true;
}

toolkit::EventPoller::Ptr RtpProcess::getOwnerPoller(MediaSource &sender) {
    if (_sock) {
        return _sock->getPoller();
    }
    throw std::runtime_error("RtpProcess::getOwnerPoller failed:" + _media_info.stream);
}

float RtpProcess::getLossRate(MediaSource &sender, TrackType type) {
    auto expected = getExpectedPacketsInterval();
    if (!expected) {
        return -1;
    }
    return getLostInterval() * 100 / expected;
}

const toolkit::Socket::Ptr& RtpProcess::getSock() const {
    return _sock;
}

void RtpProcess::openRtpDumpFile(const std::string &prefix, std::shared_ptr<FILE> &file) {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    // 取整到当前小时的起点
    _last_dump_hour_tm = time_t_now - (time_t_now % 3600);
    std::tm *tm = std::localtime(&time_t_now);

    // 查找不重复的文件名（处理中断重连场景）
    // 手动录制精确到秒，自动录制精确到小时
    int seq = 0;
    char path[1024];
    bool manual = !prefix.empty();
    while (true) {
        if (seq == 0) {
            if (manual) {
                snprintf(path, sizeof(path), "%s/%04d/%02d/%s%s_%02d_%02d_%02d_%02d_%02d.rtp",
                         _dump_dir.c_str(),
                         tm->tm_year + 1900, tm->tm_mon + 1,
                         prefix.c_str(), _media_info.stream.c_str(),
                         tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
            } else {
                snprintf(path, sizeof(path), "%s/%04d/%02d/%s_%02d_%02d_%02d.rtp",
                         _dump_dir.c_str(),
                         tm->tm_year + 1900, tm->tm_mon + 1,
                         _media_info.stream.c_str(),
                         tm->tm_mon + 1, tm->tm_mday, tm->tm_hour);
            }
        } else {
            if (manual) {
                snprintf(path, sizeof(path), "%s/%04d/%02d/%s%s_%02d_%02d_%02d_%02d_%02d_%d.rtp",
                         _dump_dir.c_str(),
                         tm->tm_year + 1900, tm->tm_mon + 1,
                         prefix.c_str(), _media_info.stream.c_str(),
                         tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, seq);
            } else {
                snprintf(path, sizeof(path), "%s/%04d/%02d/%s_%02d_%02d_%02d_%d.rtp",
                         _dump_dir.c_str(),
                         tm->tm_year + 1900, tm->tm_mon + 1,
                         _media_info.stream.c_str(),
                         tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, seq);
            }
        }
        // 检查文件是否已存在
        FILE *test_fp = fopen(path, "rb");
        if (!test_fp) {
            break; // 文件不存在，可以使用
        }
        fclose(test_fp);
        seq++;
    }

    file.reset(File::create_file(path, "wb"), [](FILE *fp) {
        fclose(fp);
    });
    if (file) {
        InfoL << "[RtpProcess] dump file opened: " << path;
    } else {
        WarnL << "[RtpProcess] failed to open dump file: " << path;
    }
}

void RtpProcess::startAutoDump(const std::string &dump_dir) {
    _dump_dir = dump_dir;
    _auto_enabled = true;
    openRtpDumpFile("", _save_file_rtp_auto);
    InfoL << "[RtpProcess] startAutoDump: " << _media_info.stream << " -> " << _dump_dir;
}

void RtpProcess::startManualDump(const std::string &dump_dir) {
    _dump_dir = dump_dir;
    _manual_enabled = true;
    openRtpDumpFile("manual_", _save_file_rtp_manual);
    InfoL << "[RtpProcess] startManualDump: " << _media_info.stream << " -> " << _dump_dir;
}

void RtpProcess::stopDump() {
    _manual_enabled = false;
    _save_file_rtp_manual.reset();
    InfoL << "[RtpProcess] stopDump: " << _media_info.stream;
}

bool RtpProcess::isDumping() const {
    return _manual_enabled;
}

void RtpProcess::checkDumpRotate() {
    if (!_auto_enabled && !_manual_enabled) {
        return;
    }
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto current_hour_tm = time_t_now - (time_t_now % 3600);

    if (current_hour_tm != _last_dump_hour_tm) {
        InfoL << "[RtpProcess] hour change detected, stream=" << _media_info.stream
              << ", auto=" << _auto_enabled << ", manual=" << _manual_enabled;

        // 轮转自动 dump 文件
        if (_auto_enabled) {
            _save_file_rtp_auto.reset();
            openRtpDumpFile("", _save_file_rtp_auto);
        }
        // 轮转手动 dump 文件
        if (_manual_enabled) {
            _save_file_rtp_manual.reset();
            openRtpDumpFile("manual_", _save_file_rtp_manual);
        }
        _last_dump_hour_tm = current_hour_tm;
    }
}

}//namespace mediakit
#endif//defined(ENABLE_RTPPROXY)