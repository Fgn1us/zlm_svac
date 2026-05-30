#include "SVACEncoder.h"

namespace mediakit {


CodecId SvacTrack::getCodecId() const {
 return CodecSVACV; // 对应 Frame.h里的 CodecSVACV
}

bool SvacTrack::ready() const {
    // 判断 SVACV Track 是否准备好
    return true; // 如果有额外条件，请补充逻辑   
}

Track::Ptr SvacTrack::clone() const {
    return std::make_shared<SvacTrack>(*this);
}

Sdp::Ptr SvacTrack::getSdp(uint8_t payload_type) const {
    return Sdp::Ptr();
}

} // namespace mediakit
