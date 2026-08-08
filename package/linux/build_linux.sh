#!/usr/bin/env bash
# ============================================================================
# ZLMediaKit SVAC fork — Linux / 国产化系统构建脚本
#
# 自动适配包管理器:
#   apt : Ubuntu / Debian / 银河麒麟(桌面版) / 统信 UOS
#   yum/dnf : openEuler / 银河麒麟(服务器版) / CentOS
#
# 用法:
#   ./package/linux/build_linux.sh [--webrtc] [--sctp] [--debug] [--jobs N]
#   --webrtc  开启 WebRTC(需 libsrtp2 开发库, 默认关闭)
#   --sctp    开启 SCTP(需 libsctp 开发库, 默认关闭)
#   --debug   Debug 构建(默认 Release)
#   --jobs N  并行编译线程数(默认取 nproc)
#   环境变量 EXTRA_CMAKE_ARGS 可透传额外 CMake 参数
# ============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

ENABLE_WEBRTC=OFF
ENABLE_SCTP=OFF
CMAKE_BUILD_TYPE=Release
JOBS="$(nproc 2>/dev/null || echo 4)"

usage() {
    echo "用法: $0 [--webrtc] [--sctp] [--debug] [--jobs N]"
    echo "  --webrtc  开启 WebRTC(需 libsrtp2 开发库)"
    echo "  --sctp    开启 SCTP(需 libsctp 开发库)"
    echo "  --debug   Debug 构建(默认 Release)"
    echo "  --jobs N  并行编译线程数(默认 nproc)"
    echo "  -h/--help 显示帮助"
    echo "环境变量 EXTRA_CMAKE_ARGS 可透传额外 CMake 参数"
    exit "${1:-0}"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --webrtc) ENABLE_WEBRTC=ON; shift ;;
        --sctp)   ENABLE_SCTP=ON;   shift ;;
        --debug)  CMAKE_BUILD_TYPE=Debug; shift ;;
        --jobs|-j)
            [[ $# -ge 2 ]] || { echo "[ERR] $1 需要参数"; exit 1; }
            JOBS="$2"; shift 2 ;;
        -h|--help) usage 0 ;;
        *) echo "[ERR] 未知参数: $1"; usage 1 ;;
    esac
done

cd "$REPO_ROOT"

echo "==> 仓库目录: $REPO_ROOT"
echo "==> 构建类型: $CMAKE_BUILD_TYPE, WebRTC=$ENABLE_WEBRTC, SCTP=$ENABLE_SCTP, jobs=$JOBS"

# ---------------------------------------------------------------------------
# 1. 检测包管理器
# ---------------------------------------------------------------------------
if command -v apt-get >/dev/null 2>&1; then
    PKG_MGR=apt
elif command -v dnf >/dev/null 2>&1; then
    PKG_MGR=dnf
elif command -v yum >/dev/null 2>&1; then
    PKG_MGR=yum
else
    echo "[ERR] 未检测到支持的包管理器(apt/dnf/yum)，请手动安装编译依赖后重试。"
    exit 1
fi
echo "==> 检测到包管理器: $PKG_MGR"

SUDO=""
[[ $EUID -eq 0 ]] || SUDO="sudo"

# ---------------------------------------------------------------------------
# 2. 安装编译依赖
# ---------------------------------------------------------------------------
install_deps() {
    local pkgs=()
    case "$PKG_MGR" in
        apt)
            pkgs=(build-essential cmake git libssl-dev)
            [[ "$ENABLE_WEBRTC" == "ON" ]] && pkgs+=(libsrtp2-dev)
            ;;
        dnf|yum)
            pkgs=(gcc gcc-c++ make cmake git openssl-devel)
            [[ "$ENABLE_WEBRTC" == "ON" ]] && pkgs+=(libsrtp-devel)
            ;;
    esac
    if [[ "$PKG_MGR" == "apt" ]]; then
        $SUDO apt-get update
        $SUDO apt-get install -y "${pkgs[@]}"
    else
        $SUDO $PKG_MGR install -y "${pkgs[@]}"
    fi
}

# 头文件存在性检查
have_header() { echo "#include <$1>" | cc -E - >/dev/null 2>&1; }

missing=()
for t in cc g++ cmake git; do
    command -v "$t" >/dev/null 2>&1 || missing+=("$t")
done
have_header openssl/ssl.h || missing+=("openssl-dev")
if [[ "$ENABLE_WEBRTC" == "ON" ]]; then
    have_header srtp2/srtp.h || missing+=("libsrtp2-dev")
fi

if [[ ${#missing[@]} -gt 0 ]]; then
    echo "==> 缺少编译依赖: ${missing[*]}，开始安装..."
    install_deps
else
    echo "==> 编译依赖已就绪"
fi

# ---------------------------------------------------------------------------
# 3. 初始化子模块(仅 3rdpart/ZLToolKit 为 gitlink, 源为 gitee)
# ---------------------------------------------------------------------------
if [[ -d "$REPO_ROOT/.git" || -f "$REPO_ROOT/.git" ]]; then
    echo "==> 初始化子模块..."
    if ! git submodule update --init --recursive; then
        echo "[WARN] submodule update 失败(可能无法访问 gitee)。"
        echo "[WARN] 若 3rdpart/ZLToolKit 目录已有源码可继续，否则下方编译将失败。"
    fi
else
    echo "==> 未检测到 .git，跳过 submodule update(假定 3rdpart/ZLToolKit 已随源码就绪)"
fi

# ---------------------------------------------------------------------------
# 4. CMake 配置
# ---------------------------------------------------------------------------
# 若 build/ 缓存来自其它平台(如 Windows VS), generator 不匹配会导致 cmake 失败, 主动清掉
if [[ -f build/CMakeCache.txt ]] && ! grep -q "CMAKE_SYSTEM_NAME:INTERNAL=Linux" build/CMakeCache.txt 2>/dev/null; then
    echo "[WARN] build/CMakeCache.txt 非 Linux 平台生成，删除后重建"
    rm -rf build
fi

echo "==> 配置 CMake..."
cmake -B build -DCMAKE_BUILD_TYPE="$CMAKE_BUILD_TYPE" \
    -DENABLE_TESTS=OFF \
    -DENABLE_API=OFF \
    -DENABLE_FFMPEG=OFF \
    -DENABLE_PLAYER=OFF \
    -DENABLE_WEBRTC="$ENABLE_WEBRTC" \
    -DENABLE_SCTP="$ENABLE_SCTP" \
    -DENABLE_OPENSSL=ON \
    -DENABLE_RTPPROXY=ON \
    -DENABLE_HLS=ON \
    -DENABLE_MP4=ON \
    -DENABLE_SERVER=ON \
    ${EXTRA_CMAKE_ARGS:-}

# ---------------------------------------------------------------------------
# 5. 编译
# ---------------------------------------------------------------------------
echo "==> 开始编译 (jobs=$JOBS, 首次编译约需数分钟)..."
cmake --build build -j "$JOBS"

echo ""
echo "============================================================================"
echo "构建完成！产物目录: $REPO_ROOT/release/linux/$CMAKE_BUILD_TYPE/"
echo "  MediaServer: $REPO_ROOT/release/linux/$CMAKE_BUILD_TYPE/MediaServer"
echo ""
echo "本地快速测试:"
echo "  cd $REPO_ROOT/release/linux/$CMAKE_BUILD_TYPE"
echo "  cp $REPO_ROOT/package/linux/config.ini.linux ./config.ini"
echo "  ./MediaServer -c config.ini -s default.pem -l 0"
echo "正式部署请执行: ./package/linux/pack_linux.sh"
echo "============================================================================"
