#!/usr/bin/env bash
# ============================================================================
# ZLMediaKit SVAC fork — Linux 打包脚本
# 将 build_linux.sh 的产物打成 tar.gz(替换为 Linux 版配置), 供裸机/内网部署
#
# 用法:
#   ./package/linux/pack_linux.sh [-v VERSION] [--debug]
#   -v VERSION 指定版本号(默认取 git describe --tags, 失败则 dev)
#   --debug    打包 Debug 产物(默认 Release)
#
# 产物: zlm-svac_linux_<arch>_<version>.tar.gz (位于仓库根目录)
# 布局:
#   zlm-svac/
#   ├── bin/MediaServer
#   ├── conf/config.ini        (Linux 模板)
#   ├── www/
#   └── default.pem
# ============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

VERSION=""
BUILD_TYPE=Release

usage() {
    echo "用法: $0 [-v VERSION] [--debug]"
    echo "  -v VERSION 指定版本号(默认 git describe)"
    echo "  --debug    打包 Debug 产物(默认 Release)"
    exit "${1:-0}"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -v)
            [[ $# -ge 2 ]] || { echo "[ERR] -v 需要参数"; exit 1; }
            VERSION="$2"; shift 2 ;;
        --debug) BUILD_TYPE=Debug; shift ;;
        -h|--help) usage 0 ;;
        *) echo "[ERR] 未知参数: $1"; usage 1 ;;
    esac
done

RELEASE_DIR="$REPO_ROOT/release/linux/$BUILD_TYPE"
if [[ ! -x "$RELEASE_DIR/MediaServer" ]]; then
    echo "[ERR] 找不到产物 $RELEASE_DIR/MediaServer"
    echo "      请先在 Linux 上运行: ./package/linux/build_linux.sh"
    exit 1
fi

[[ -d "$RELEASE_DIR/www" ]] || { echo "[ERR] $RELEASE_DIR/www 不存在(构建不完整)"; exit 1; }
[[ -f "$RELEASE_DIR/default.pem" ]] || { echo "[ERR] $RELEASE_DIR/default.pem 不存在"; exit 1; }

if [[ -z "$VERSION" ]]; then
    VERSION="$(git -C "$REPO_ROOT" describe --tags --always 2>/dev/null || echo dev)"
fi

ARCH="$(uname -m)"
case "$ARCH" in
    x86_64|amd64) ARCH=x86_64 ;;
    aarch64|arm64) ARCH=aarch64 ;;
esac

PKG_NAME="zlm-svac_linux_${ARCH}_${VERSION}.tar.gz"
PKG_PATH="$REPO_ROOT/$PKG_NAME"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT

echo "==> 收集产物: $RELEASE_DIR"
mkdir -p "$STAGE/zlm-svac/bin" "$STAGE/zlm-svac/conf"
cp "$RELEASE_DIR/MediaServer" "$STAGE/zlm-svac/bin/"
cp -r "$RELEASE_DIR/www" "$STAGE/zlm-svac/www"
cp "$RELEASE_DIR/default.pem" "$STAGE/zlm-svac/"
# 用 Linux 配置模板替换构建时带入的 Windows 版 config.ini
cp "$SCRIPT_DIR/config.ini.linux" "$STAGE/zlm-svac/conf/config.ini"
chmod +x "$STAGE/zlm-svac/bin/MediaServer"

echo "==> 打包 $PKG_PATH (arch=$ARCH, version=$VERSION)"
tar -C "$STAGE" -czf "$PKG_PATH" zlm-svac

echo ""
echo "打包完成: $PKG_PATH"
echo "部署: 拷贝 $PKG_NAME 与 package/linux/install_linux.sh、zlm-svac.service 到目标机,"
echo "      运行: ./install_linux.sh $PKG_NAME"
