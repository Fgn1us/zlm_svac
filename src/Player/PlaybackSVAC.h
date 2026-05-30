/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLMEDIAKIT_PLAYBACKSVAC_H
#define ZLMEDIAKIT_PLAYBACKSVAC_H

#if defined(ENABLE_RTPPROXY)

#include <cstdio>
#include <vector>
#include "Rtp/RtpCache.h"
#include "Common/MediaSource.h"

namespace mediakit {

class PlaybackSVAC : public std::enable_shared_from_this<PlaybackSVAC> {
public:
    using Ptr = std::shared_ptr<PlaybackSVAC>;
    using onCloseCB = std::function<void(const toolkit::SockException &ex)>;
    using onStartCB = std::function<void(uint16_t local_port, const toolkit::SockException &ex)>;

    PlaybackSVAC(toolkit::EventPoller::Ptr poller = nullptr);
    ~PlaybackSVAC();

    void start(const std::string &file_path, const MediaSourceEvent::SendRtpArgs &args, onStartCB cb);
    void stop();
    bool isRunning() const { return !_stopped && _is_connect; }
    void setOnClose(onCloseCB cb) { _on_close = std::move(cb); }

private:
    struct PackEntry {
        uint64_t offset;
        int64_t scr_ms;
    };

    struct RtpEntry {
        uint64_t offset;
        uint16_t size;
        int64_t stamp_ms;
    };

    void indexFile();
    void indexRtpFile();
    bool onTimer();
    void feedNextChunk();
    void feedNextRtp();
    int64_t extractSCR(const uint8_t *data);

    void startUdpActive(onStartCB cb);
    void onConnect();
    void onFlushRtpList(std::shared_ptr<toolkit::List<toolkit::Buffer::Ptr>> rtp_list);

    toolkit::EventPoller::Ptr _poller;
    toolkit::Socket::Ptr _socket_rtp;
    MediaSinkInterface::Ptr _rtp_encoder;
    toolkit::Timer::Ptr _play_timer;
    std::shared_ptr<FILE> _file;
    bool _is_connect = false;
    bool _stopped = false;
    MediaSourceEvent::SendRtpArgs _args;
    onCloseCB _on_close;
    std::string _file_path;

    bool _raw_mode = false;
    std::vector<PackEntry> _index;
    std::vector<RtpEntry> _rtp_index;
    size_t _current_idx = 0;
    int64_t _base_scr_ms = 0;
    toolkit::Ticker _play_ticker;
};

} // namespace mediakit
#endif // defined(ENABLE_RTPPROXY)
#endif // ZLMEDIAKIT_PLAYBACKSVAC_H
