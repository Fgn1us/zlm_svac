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
#include "GB35114Process.h"
#include "Extension/CommonRtp.h"
#include "Extension/Factory.h"
#include "Http/HttpTSPlayer.h"
#include "Util/File.h"
#include "Common/config.h"
#include "Rtsp/RtpReceiver.h"
#include "Rtsp/Rtsp.h"
#include <Codec/SVACEncoder.h>

using namespace std;
using namespace toolkit;

namespace mediakit {

// 判断是否为ts负载  [AUTO-TRANSLATED:77d1aa3c]
// Determine if it is a ts payload
static inline bool checkTS(const uint8_t *packet, size_t bytes) {
    return bytes % TS_PACKET_SIZE == 0 && packet[0] == TS_SYNC_BYTE;
}

class RtpReceiverImp : public RtpTrackImp {
public:
    using Ptr = std::shared_ptr<RtpReceiverImp>;

    RtpReceiverImp(int sample_rate, RtpTrackImp::OnSorted cb, RtpTrackImp::BeforeSorted cb_before = nullptr) {
        _sample_rate = sample_rate;
        setOnSorted(std::move(cb));
        setBeforeSorted(std::move(cb_before));
        // GB28181推流不支持ntp时间戳  [AUTO-TRANSLATED:f661f052]
        // GB28181 streaming does not support ntp timestamps
        setNtpStamp(0, 0);
    }

    bool inputRtp(TrackType type, uint8_t *ptr, size_t len) {
        return RtpTrack::inputRtp(type, _sample_rate, ptr, len).operator bool();
    }

private:
    int _sample_rate;
};

///////////////////////////////////////////////////////////////////////////////////////////

GB35114Process::GB35114Process(const MediaInfo &media_info, MediaSinkInterface *sink) {
    assert(sink);
    // InfoL << "App: " << _media_info.app << ", Stream: " << _media_info.stream; // no information
    _media_info = media_info;
    _interface = sink;
    _pub_key = nullptr;
}

void GB35114Process::onRtpSorted(RtpPacket::Ptr rtp) {
    _rtp_decoder[rtp->getHeader()->pt]->inputRtp(rtp, false);
}

void GB35114Process::flush() {
    if (_decoder) {
        _decoder->flush();
    }
}

bool GB35114Process::inputRtp(bool, const char *data, size_t data_len) {
    //以上为新增内容
    GET_CONFIG(uint32_t, h264_pt, RtpProxy::kH264PT);
    GET_CONFIG(uint32_t, h265_pt, RtpProxy::kH265PT);
    GET_CONFIG(uint32_t, ps_pt, RtpProxy::kPSPT);
    GET_CONFIG(uint32_t, opus_pt, RtpProxy::kOpusPT);

    RtpHeader *header = (RtpHeader *)data;
    //header->pt = 98; // 强制99负载类型  [AUTO-TRANSLATED:3f3e2f6c]
    auto pt = header->pt;
   
    // DebugL << "X412 data length: " << data_len << " pt:" << pt;
    auto &ref = _rtp_receiver[pt];
    if (!ref) {
        if (_rtp_receiver.size() > 2) {
            // 防止pt类型太多导致内存溢出  [AUTO-TRANSLATED:7695e49b]
            // Prevent too many pt types from causing memory overflow
            WarnL << "Rtp payload type more than 2 types: " << _rtp_receiver.size();
        }

        do {
            if (pt < 96) {
                auto codec = RtpPayload::getCodecId(pt);
                if (codec != CodecInvalid && codec != CodecTS) {
                    auto sample_rate = RtpPayload::getClockRate(pt);
                    auto channels = RtpPayload::getAudioChannel(pt);
                    ref = std::make_shared<RtpReceiverImp>(sample_rate, [this](RtpPacket::Ptr rtp) { onRtpSorted(std::move(rtp)); });
                    auto track = Factory::getTrackByCodecId(codec, sample_rate, channels);
                    CHECK(track);
                    track->setIndex(pt);
                    _interface->addTrack(track);
                    _rtp_decoder[pt] = Factory::getRtpDecoderByCodecId(track->getCodecId());
                    break;
                }
            }
            if (pt == opus_pt) {
                // opus负载  [AUTO-TRANSLATED:defa6a8d]
                // opus payload
                ref = std::make_shared<RtpReceiverImp>(48000, [this](RtpPacket::Ptr rtp) { onRtpSorted(std::move(rtp)); });
                auto track = Factory::getTrackByCodecId(CodecOpus);
                CHECK(track);
                track->setIndex(pt);
                _interface->addTrack(track);
                _rtp_decoder[pt] = Factory::getRtpDecoderByCodecId(track->getCodecId());
                break;
            }
            if (pt == h265_pt) {
                // H265负载  [AUTO-TRANSLATED:61fbcf7f]
                // H265 payload
                ref = std::make_shared<RtpReceiverImp>(90000, [this](RtpPacket::Ptr rtp) { onRtpSorted(std::move(rtp)); });
                auto track = Factory::getTrackByCodecId(CodecH265);
                CHECK(track);
                track->setIndex(pt);
                _interface->addTrack(track);
                _rtp_decoder[pt] = Factory::getRtpDecoderByCodecId(track->getCodecId());
                break;
            }
            if (pt == h264_pt) {
                // H264负载  [AUTO-TRANSLATED:6f3fbb0d]
                // H264 payload
                ref = std::make_shared<RtpReceiverImp>(90000, [this](RtpPacket::Ptr rtp) { onRtpSorted(std::move(rtp)); });
                auto track = Factory::getTrackByCodecId(CodecH264);
                CHECK(track);
                track->setIndex(pt);
                _interface->addTrack(track);
                _rtp_decoder[pt] = Factory::getRtpDecoderByCodecId(track->getCodecId());
                break;
            }



            if (pt != Rtsp::PT_MP2T && pt != ps_pt) {
                WarnL << "Unknown rtp payload type(" << (int)pt << "), decode it as mpeg-ps or mpeg-ts";
            }
            ref = std::make_shared<RtpReceiverImp>(90000, [this](RtpPacket::Ptr rtp) { onRtpSorted(std::move(rtp)); });

            // auto track = Factory::getTrackByCodecId(CodecSVACV);
            auto track = std::make_shared<SvacTrack>();
            CHECK(track);
            track->setIndex(pt);
            _interface->addTrack(track);

            // ts或ps负载
            // ts or ps payload
            // 明确指定 CodecPS，使输出 Frame 类型正确
            // Explicitly specify CodecPS to ensure correct output Frame type
            _rtp_decoder[pt] = std::make_shared<CommonRtpDecoder>(CodecPS, 32 * 1024);
            // 设置dump目录
            // Set dump directory
            GET_CONFIG(string, dump_dir, RtpProxy::kDumpDir);

            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            std::stringstream ss;
            ss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");

            if (!dump_dir.empty()) {
                auto save_path = File::absolutePath(_media_info.stream + "_" + ss.str() + ".mpeg", dump_dir);
                _save_file_ps.reset(File::create_file(save_path.data(), "wb"), [](FILE *fp) {
                    if (fp) {
                        fclose(fp);
                    }
                });
            }
            
            // [AUTO-TRANSLATED: Complete Track Adding]
            // 立即通知 Track 添加完成，因为我们不会再添加其他 Track
            // Immediately notify Track addition completion, because we will not add other Tracks
            _interface->addTrackCompleted();
        } while (false);

        // 设置frame回调  [AUTO-TRANSLATED:dec7590f]
        // Set frame callback
        _rtp_decoder[pt]->addDelegate([this, pt](const Frame::Ptr &frame) {
            frame->setIndex(pt);
            onRtpDecode(frame);
            return true;
        });
    }

    return ref->inputRtp(TrackVideo, (unsigned char *)data, data_len);
}

void GB35114Process::onRtpDecode(const Frame::Ptr &frame) {
    switch (frame->getCodecId()) {
        case CodecInvalid:
        case CodecSVACV:
        case CodecTS:
        case CodecPS: break;

        default:
            // 这里不是ps或ts  [AUTO-TRANSLATED:6f79ac69]
            // This is not ps or ts
            _interface->inputFrame(frame);
            return;
    }

    bool vr = verifyVideoFrame(frame); 
    if (!vr) { // 验签失败
        WarnL << "GB35114Process verified failed -> frame is dropped!";
        return; // 直接跳过不转发
    }

    // 这是TS或PS保存
    if (_save_file_ps) {
        fwrite(frame->data(), frame->size(), 1, _save_file_ps.get());
    }
    
    _interface->inputFrame(frame);
    return; 

    /* 
     * 原始逻辑：创建解码器解析 PS/TS (Disabled for Passthrough)
     * Original logic: Create decoder to parse PS/TS
     */
    /*
    if (!_decoder) {
        // 创建解码器
        // Create decoder
        if (checkTS((uint8_t *)frame->data(), frame->size())) {
            // 猜测是ts负载
            // Guess it is a ts payload
            InfoL << _media_info.stream << " judged to be TS";
            _decoder = DecoderImp::createDecoder(DecoderImp::decoder_ts, _interface);
        } else {
            // 猜测是ps负载
            // Guess it is a ps payload
            InfoL << _media_info.stream << " judged to be PS";
            _decoder = DecoderImp::createDecoder(DecoderImp::decoder_ps, _interface);
        }
    }

    // 此处解码并转发
    if (_decoder) {
        _decoder->input(reinterpret_cast<const uint8_t *>(frame->data()), frame->size());
    }
    _interface->inputFrame(frame); // 转发原始数据
    */
}

// 验签视频帧是否有效，返回true表示有效  [AUTO-TRANSLATED:6f3e2f3f]
bool GB35114Process::verifyVideoFrame(const Frame::Ptr &frame) {

    // 第一版直接转发，后续版本完善验签逻辑  [AUTO-TRANSLATED:8b9a0c1d]
    return true;
    // 准备验签公钥  [AUTO-TRANSLATED:1a2b3c4d]
    // 未配置验签公钥，加载/获取验签公钥  [AUTO-TRANSLATED:3e1f4c2e]
    if (!_pub_key) {
        // get cert_file_data string from X509 cert file or stream

        // cert_file_path_data 从配置文件获取  [AUTO-TRANSLATED:5d6e7f8a]
        GET_CONFIG(string, cert_file_path_data, RtpProxy::kVerifyCertFilePath);
        auto X509_list = SSLUtil::loadPublicKey(cert_file_path_data, "", true);
        // 如果从数据流获取证书，则调用SSLUtil::loadPublicKey加载时最后一个参数isFile为false
        // string cert_file_path_data = "";
        // auto X509_list = SSLUtil::loadPublicKey(cert_file_path_data, "", false);
        
        if (X509_list.empty()) { // 加载公钥失败  [AUTO-TRANSLATED:2e1a4b5c]
            WarnL << "Load public key failed for verify video frame!";
            return false;
        }
            
        _pub_key = X509_get_pubkey(X509_list[0].get());
        if (!_pub_key) // 加载公钥失败  [AUTO-TRANSLATED:2e1a4b5c]
            return false;
    }
    const char *data = frame->data();
    size_t len = frame->size();
    bool vr = false;
    if (!data || len < 10) { // 数据无效  [AUTO-TRANSLATED:9f8e7d6c]
        return false;
    }
    //TODO: 验签逻辑， 如果通过则设置vr=true [AUTO-TRANSLATED:4b5c6d7e]
    //TODO: vr = verify(data, len, _pub_key)
    return vr;
}

} // namespace mediakit
#endif // defined(ENABLE_RTPPROXY)
