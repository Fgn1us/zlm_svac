# ZLMediaKit SVAC fork — Linux / 国产化系统部署指南

本仓库是 ZLMediaKit 的 SVAC + GB35114 扩展版本。本文档说明如何将其编译、打包并部署到 **Linux（含麒麟 / openEuler 等国产化系统）**。

---

## 0. 能力边界（重要）

| 能力 | 当前状态 |
|---|---|
| SVAC 流接入（GB28181 拉流 / RTP 推流） | ✅ 支持 |
| SVAC 流**透传**转发（RTSP/RTMP/HLS 等） | ✅ 支持 |
| SVAC 流 **dump 录制** 与 **GB35114 回放**（`playback_svac`） | ✅ 支持（路径见 `[rtp_proxy] dumpDir`） |
| SVAC **解码**（画面预览） | ❌ 未接入（SDK 为 Windows DLL，未参与构建） |
| GB35114 **验签** | ❌ stub（`verifyVideoFrame()` 直接返回 true） |

> 若需要在 Linux 上做真正的 SVAC 解码 / GB35114 验签，须向解码器厂商索取 **Linux(.so) 版 SDK** 并做接入开发。当前透传 + 录制模式无需任何厂商 SDK。

---

## 1. 源码获取

> 关键：`3rdpart/ZLToolKit` 是 git 子模块（gitee 源），**必须**保证其源码存在，否则编译失败。

### 方式 A：git clone（推荐，能访问外网 / gitee）
```bash
git clone <你的仓库地址> zlm_svac
cd zlm_svac
git submodule update --init --recursive
```
`media-server`、`jsoncpp` 已直接入库，无需额外拉取。

### 方式 B：整目录拷贝（内网隔离环境）
把整个项目目录（含 `3rdpart/ZLToolKit`）用 SCP / U 盘拷到目标机。**务必确认 `3rdpart/ZLToolKit/src/` 目录下有 Network、Poller 等源码**。

> `3rdpart/ZXSvacDec/` 目录是 **Windows 版 DLL**，Linux 上无用，可删以减小体积（不影响编译）。

---

## 2. 编译（目标机或编译机）

脚本会自动识别包管理器（apt：Ubuntu/Debian/麒麟桌面/统信；yum/dnf：openEuler/麒麟服务器/CentOS）并安装依赖。

```bash
# 首次使用给脚本加执行权限（从 Windows 拷贝的脚本默认无执行位）
chmod +x package/linux/*.sh

# 编译
./package/linux/build_linux.sh
```

常用参数：
```bash
./package/linux/build_linux.sh --webrtc      # 开启 WebRTC（需 libsrtp2 开发库）
./package/linux/build_linux.sh --sctp        # 开启 SCTP
./package/linux/build_linux.sh --debug       # Debug 构建
./package/linux/build_linux.sh -j 8          # 指定并行线程
```

**默认关闭** WebRTC / SCTP / FFmpeg（SVAC 透传场景不需要），编译依赖最小化。产物输出到：

```
release/linux/Release/MediaServer
```

### 本地快速自测
```bash
cd release/linux/Release
cp ../../../../package/linux/config.ini.linux ./config.ini
./MediaServer -c config.ini -s default.pem -l 0
```

---

## 3. 部署方式一：裸机 tar 包 + systemd

### 3.1 打包
```bash
./package/linux/pack_linux.sh            # 版本号默认取 git describe
./package/linux/pack_linux.sh -v 1.0.0   # 手动指定版本
./package/linux/pack_linux.sh --debug    # 打包 Debug 产物
```
生成 `zlm-svac_linux_<arch>_<version>.tar.gz`，内部布局：
```
zlm-svac/
├── bin/MediaServer
├── conf/config.ini        # Linux 版配置（已替换 Windows 路径）
├── www/
└── default.pem
```

### 3.2 安装
把 `tar 包`、`package/linux/install_linux.sh`、`package/linux/zlm-svac.service` 三个文件拷到目标机，然后：
```bash
chmod +x install_linux.sh
./install_linux.sh zlm-svac_linux_x86_64_1.0.0.tar.gz
```
脚本会：
- 解压到 `/opt/zlm_svac/`，创建 `/opt/zlm_svac/dump/disk1`
- 注册 systemd 服务 `zlm-svac` 并立即启动

### 3.3 systemd 运维
```bash
systemctl status zlm-svac        # 查看状态
journalctl -u zlm-svac -f        # 跟踪日志
systemctl restart zlm-svac       # 重启（改配置后）
systemctl stop|start zlm-svac    # 停止/启动
./package/linux/install_linux.sh --uninstall   # 卸载
```

### 3.4 配置修改
配置文件：`/opt/zlm_svac/conf/config.ini`

需要关注的项：
```ini
[rtp_proxy]
dumpDir=/opt/zlm_svac/dump/disk1     # SVAC dump/录制根目录（部署时可改）
storageRoot=/opt/zlm_svac/dump
ps_pt=96                             # SVAC PS 负载类型
h264_pt=98
h265_pt=99

[hook]
on_playback_svac=http://wvp-ip:port/xxx/playback_svac   # 改成实际 WVP 平台地址
enable=0                             # 启用 hook 后置 1
```
改完执行 `systemctl restart zlm-svac`。

---

## 4. 部署方式二：Docker

已修复原 `dockerfile` 的问题（子模块、libsrtp 下载源、FFMPEG 空转、runtime 缺库），并新增 `.dockerignore` 减小构建上下文。

```bash
# 构建（宿主机需能访问 gitee 拉子模块）
docker build --network=host --build-arg MODEL=Release -t zlm-svac:latest -f dockerfile .

# 运行（映射端口）
docker run -d --name zlm-svac --network=host \
  --restart=always zlm-svac:latest
```
或仅映射必要端口：
```bash
docker run -d --name zlm-svac -p 554:554 -p 1935:1935 -p 80:80 \
  -p 10000:10000/udp -p 30000-35000:30000-35000/udp \
  -v /srv/zlm_svac/dump:/opt/media/dump \
  --restart=always zlm-svac:latest
```

> 容器内配置位于 `/opt/media/conf/config.ini`，dump 路径已自动改为 `/opt/media/dump`。若需持久化 dump 数据，挂载卷如上示例。

---

## 5. 端口清单

| 端口 | 协议 | 用途 |
|---|---|---|
| 554 | TCP | RTSP |
| 1935 | TCP | RTMP |
| 80 / 443 | TCP | HTTP / HTTPS（Web API、网页管理） |
| 10000 | UDP | RTP 流接收（`[rtp_proxy] port`） |
| 30000-35000 | UDP | RTP 流端口范围（`[rtp_proxy] port_range`） |
| 8000 | UDP/TCP | WebRTC（未启用则无效） |
| 9000 | UDP | SRT（`[srt] port`） |

防火墙放行请按需配置。Web API 默认无鉴权（`api.secret` 已配置，调用时需带 secret）。

---

## 6. 常见问题

**Q1: 编译报错找不到 `ZLToolKit` 头文件**
→ `3rdpart/ZLToolKit/src` 为空，先执行 `git submodule update --init --recursive`，或确认拷贝源码时带上了该目录。

**Q2: 无法访问 gitee 导致 submodule 拉取失败**
→ 在能联网的机器上 checkout 好子模块后整目录拷贝；脚本对 submodule 失败仅为警告，只要 `3rdpart/ZLToolKit` 有源码即可继续。

**Q3: 启动后 dump/录制不落盘**
→ 检查 `conf/config.ini` 的 `dumpDir` 是否存在且可写（默认 `/opt/zlm_svac/dump/disk1`）；`File::create_file` 不会自动创建父目录，路径无效时功能静默失效（仅日志 Warn）。

**Q4: 启动报 `secret` 为默认值 / 空**
→ `main.cpp` 会拒绝用默认 secret 启动，在 `[api] secret` 配置一个随机串。

**Q5: 需要 WebRTC 功能**
→ 安装 `libsrtp2-dev` 后用 `./package/linux/build_linux.sh --webrtc` 重新编译；国产化源无此包时用源码编译 libsrtp 或改用系统包源。

**Q6: 端口被占用 / 监听失败**
→ 修改 `config.ini` 对应端口后重启；`systemctl status` 看日志确认。

---

## 7. 交付物清单

```
package/linux/
├── build_linux.sh        # 编译脚本（国产化 apt/yum 自适应）
├── pack_linux.sh         # tar 打包脚本
├── install_linux.sh      # 安装 + systemd 脚本（含 --uninstall）
├── zlm-svac.service      # systemd unit
└── config.ini.linux      # Linux 配置模板
dockerfile                # 已修复（子模块/srtp/FFMPEG/runtime 缺库）
.dockerignore             # 减小 Docker 构建上下文
```
