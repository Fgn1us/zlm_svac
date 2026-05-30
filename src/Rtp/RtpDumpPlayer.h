/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLMEDIAKIT_RTPDUMPPLAYER_H
#define ZLMEDIAKIT_RTPDUMPPLAYER_H

#if defined(ENABLE_RTPPROXY)

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "Network/Socket.h"

namespace mediakit {

// RTP dump 回放器，读取 .rtp dump 文件并逐包发送到远端 UDP 地址
// 按 RTP 原始时间戳间隔控速发送，模拟实时回放
class RtpDumpPlayer : public std::enable_shared_from_this<RtpDumpPlayer> {
public:
    using Ptr = std::shared_ptr<RtpDumpPlayer>;
    using onCompleteCB = std::function<void()>;
    using onErrorCB = std::function<void(const toolkit::SockException &ex)>;

    struct PlayArgs {
        std::string file_path;      // dump 文件绝对路径
        std::string dst_url;        // 目标 IP 地址
        uint16_t dst_port = 0;      // 目标 UDP 端口
        uint16_t src_port = 0;      // 本地 UDP 绑定端口 (0 = 随机)
        float speed = 1.0f;         // 播放速率，1.0 = 原始速度
    };

    RtpDumpPlayer();
    ~RtpDumpPlayer();

    /**
     * 开始回放
     * @param args 回放参数
     * @param on_complete 播放完成回调
     * @param on_error 播放出错回调
     */
    void start(const PlayArgs &args, onCompleteCB on_complete, onErrorCB on_error);

    /**
     * 停止回放
     */
    void stop();

    /**
     * 是否正在播放
     */
    bool isPlaying() const;

    /**
     * 获取已发送包数
     */
    uint64_t getSentPackets() const;

    /**
     * 获取已发送字节数
     */
    uint64_t getSentBytes() const;

    /**
     * 获取总时长（毫秒）
     */
    uint64_t getDurationMs() const;

    /**
     * 获取总包数
     */
    uint64_t getTotalPackets() const;

    /**
     * 获取本地绑定端口
     */
    uint16_t getLocalPort() const;

private:
    struct ParsedPacket {
        uint32_t raw_timestamp;          // RTP 时间戳（主机序）
        uint64_t offset_ms;              // 相对起始包的累计毫秒偏移
        toolkit::Buffer::Ptr data;       // RTP 原始数据（无 2 字节长度前缀）
    };

    // 解析 dump 文件，提取所有 RTP 包及时间戳
    bool parseDumpFile(const std::string &path);

    // 发送当前包并调度下一个
    void sendNextPacket();

    // 播放完成
    void onPlayCompleted();

    // 通知错误
    void notifyError(const toolkit::SockException &ex);

private:
    PlayArgs _args;
    std::string _identifier;

    bool _playing = false;
    uint64_t _sent_packets = 0;
    uint64_t _sent_bytes = 0;
    uint64_t _total_packets = 0;
    uint64_t _total_duration_ms = 0;

    std::vector<ParsedPacket> _packets;
    size_t _current_index = 0;

    toolkit::Socket::Ptr _socket;
    toolkit::EventPoller::Ptr _poller;

    onCompleteCB _on_complete;
    onErrorCB _on_error;

    // 延迟任务，用于取消调度
    toolkit::EventPoller::DelayTask::Ptr _delay_task;
};

} // namespace mediakit
#endif // defined(ENABLE_RTPPROXY)
#endif // ZLMEDIAKIT_RTPDUMPPLAYER_H
