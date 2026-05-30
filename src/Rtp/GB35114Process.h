/*
 * Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/ZLMediaKit/ZLMediaKit).
 *
 * Use of this source code is governed by MIT-like license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#ifndef ZLMEDIAKIT_GB35114PROCESS_H
#define ZLMEDIAKIT_GB35114PROCESS_H

#if defined(ENABLE_RTPPROXY)

#include "Decoder.h"
#include "ProcessInterface.h"
#include "Http/HttpRequestSplitter.h"
#include "Rtsp/RtpCodec.h"
#include "Common/MediaSource.h"
#include "Util/SSLBox.h"
#include "Util/SSLUtil.h"

#if defined(ENABLE_OPENSSL)
#include <openssl/bio.h>
#include <openssl/conf.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/ossl_typ.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#endif


namespace mediakit{

class RtpReceiverImp;
class GB35114Process : public ProcessInterface {
public:
    using Ptr = std::shared_ptr<GB35114Process>;

    GB35114Process(const MediaInfo &media_info, MediaSinkInterface *sink);

    /**
     * 输入rtp
     * @param data rtp数据指针
     * @param data_len rtp数据长度
     * @return 是否解析成功
     * Input rtp
     * @param data rtp data pointer
     * @param data_len rtp data length
     * @return Whether the parsing is successful
     
     * [AUTO-TRANSLATED:d7b14ffe]
     */
    bool inputRtp(bool, const char *data, size_t data_len) override;

    /**
     * 刷新输出所有缓存
     */
    void flush() override;

    /**
     * 开始 dump 数据到文件
     */
    bool startDump(const std::string &dump_dir, const std::string &stream_id, const std::string &prefix = "") override;

    /**
     * 停止 dump 数据
     */
    bool stopDump() override;

    /**
     * 轮转 dump 文件（关闭当前文件，打开下个整点的新文件）
     */
    bool rotateFile() override;

protected:
    void onRtpSorted(RtpPacket::Ptr rtp);

private:
    void onRtpDecode(const Frame::Ptr &frame);

    // 验签视频帧是否有效，返回true表示有效
    bool verifyVideoFrame(const Frame::Ptr &frame);

    // 打开 PS dump 文件（按小时命名）
    void openPsDumpFile();

private:
    MediaInfo _media_info;
    DecoderImp::Ptr _decoder;
    MediaSinkInterface *_interface;
    std::shared_ptr<FILE> _save_file_ps;
    std::unordered_map<uint8_t, RtpCodec::Ptr> _rtp_decoder;
    std::unordered_map<uint8_t, std::shared_ptr<RtpReceiverImp> > _rtp_receiver;
    //TODO: 验签公钥定义
#if defined(ENABLE_OPENSSL)
    EVP_PKEY *_pub_key;
#endif
    //新增
    uint32_t _mock_timestamp = 0;
    // dump 控制
    bool _dumping = false;
    std::string _dump_dir;
    std::string _stream_id;
    std::string _prefix;
    std::time_t _last_dump_hour_tm = 0;
};

}//namespace mediakit
#endif//defined(ENABLE_RTPPROXY)
#endif //ZLMEDIAKIT_GB35114ROCESS_H
