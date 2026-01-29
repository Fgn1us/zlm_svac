#pragma once

#include <memory>
#include <string>
#include "Common/MediaSource.h"

namespace mediakit {

class SvacTrack : public VideoTrack {
public:
 using Ptr = std::shared_ptr<SvacTrack>;
 SvacTrack()=default;

 CodecId getCodecId() const override;
 // 可根据需要扩展 ready、clone、getSdp 等接口
 bool ready() const override;
 Track::Ptr clone() const override;

 // 通过 VideoTrack 继承
 Sdp::Ptr getSdp(uint8_t payload_type) const override;
};

} // namespace mediakit
