# ZLMediaKit SVAC Fork - Project Context

## Project Identity
This is a fork of [ZLMediaKit](https://github.com/ZLMediaKit/ZLMediaKit), a high-performance C++11 media server. This fork adds **SVAC (GB/T 25724-2010)** codec support and **GB35114** secure video surveillance protocol support for the Chinese public security industry.

## Key Modifications (Our Work)

### 1. SVAC Codec Integration
- **Codec IDs**: `CodecSVACV`(16) for video, `CodecSVACA`(17) for audio — registered in `src/Extension/Frame.h`
- **SvacTrack**: Wrapper class in `src/Codec/SVACEncoder.h/.cpp`
- **3rd-party decoder**: ZX SVAC SDK in `3rdpart/ZXSvacDec/` (DLLs + headers)
- **PSI stream types**: `PSI_STREAM_VIDEO_SVAC=0x80`, `PSI_STREAM_AUDIO_SVAC=0x9B` (in 3rdparty media-server libmpeg)
- **RTP payload types**: SVAC video PT=99, SVAC audio PT=20 (per GB28181-2016); PT=96 mapped to SVACV in `src/Rtsp/Rtsp.h`

### 2. GB35114 Process (Secure Video Surveillance)
- **New files**: `src/Rtp/GB35114Process.h`, `src/Rtp/GB35114Process.cpp`
- Replaces `GB28181Process` as the default RTP processor (see `src/Rtp/RtpProcess.cpp:134`)
- Handles PS stream demux, creates `SvacTrack` for SVAC payloads
- **Signature verification**: `verifyVideoFrame()` — currently stubbed (always returns true), planned to use X509 cert + OpenSSL
- **PS dump**: Hourly-rotating `.mpeg` dump files via `openPsDumpFile()`

### 3. RTP Modifications
- **PS passthrough**: `RtpCachePS` in `src/Rtp/RtpCache.h` — zero-copy forwarding of pre-encoded PS data, bypasses MpegMuxer
- **TCP recovery**: `RtpSession::searchByPsHeaderFlag()` — recovers TCP framing by scanning for PS System Header (0x000001bb) when SSRC-based recovery fails
- **PS encoder/decoder**: `src/Rtp/PSEncoder.h/.cpp`, `src/Rtp/PSDecoder.h/.cpp`
- **New config keys** under `[rtp_proxy]`: `dumpDir`, `h264_pt`(98), `h265_pt`(99), `ps_pt`(96), `opus_pt`(100), `cert_file_path`, `udp_recv_socket_buffer`

### 4. Recording & Playback
- **SVAC playback**: `src/Player/PlaybackSVAC.h/.cpp` — replays `.mpeg` (PS) and `.rtp` (raw RTP) files via UDP/TCP
- **SVAC record notification**: `Broadcast::SvacRecordInfo` struct + `kBroadcastRecordSvacRtp` event in `src/Common/config.h`
- **RTP dump**: Auto-dump on stream start, hourly rotation, manual start/stop via API

### 5. Web API & WebHook
- **5 new API endpoints** in `server/WebApi.cpp` (guarded by `ENABLE_RTPPROXY && _WIN32`):
  - `/index/api/startSVACRecord` — start SVAC recording
  - `/index/api/recordSVAC` — manual RTP dump start
  - `/index/api/stopRecordSVAC` — stop SVAC recording
  - `/index/api/startPlaybackSVAC` — start SVAC file playback via RTP
  - `/index/api/stopPlaybackSVAC` — stop SVAC playback
- **WebHook**: `on_svac_record` hook in `server/WebHook.cpp` + `conf/config.ini`

### 6. Configuration
- `conf/config.ini` — added `hook.on_svac_record=`
- `conf/readme.md` — performance tuning notes

## Build System
- CMake-based, Visual Studio solution at `build/`
- Compile definition `ENABLE_RTPPROXY` required for GB35114/SVAC features
- OpenSSL headers needed for planned signature verification (currently missing)

## Architecture Notes
- **RTP processing chain**: `RtpServer` → `RtpSession` → `RtpProcess` → `GB35114Process` → `MultiMediaSourceMuxer`
- **Codec registration**: Codec IDs defined via `CODEC_MAP` macro in `Frame.h`; decoder plugins registered via `REGISTER_CODEC` in `Factory.cpp` (SVAC plugin registration is currently commented out)
- **Dump directory**: `dump/` at project root
- **Test files**: `tests/test_svac_hook.py`, `tests/test_rtp.cpp`, `tests/test_ps.cpp`

## Current Status / TODO
- [ ] Signature verification in `GB35114Process::verifyVideoFrame()` — needs X509 cert logic implemented
- [ ] OpenSSL dependency — headers not configured in build
- [ ] SVAC decoder plugin registration — commented out in `Factory.cpp`
- [ ] `REGISTER_CODEC(svac_plugin)` needs to be enabled once SVAC decoder is ready
