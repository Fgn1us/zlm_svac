#!/usr/bin/env bash
# ============================================================================
# ZLMediaKit SVAC fork — Linux 安装脚本
# 将 pack_linux.sh 打出的 tar 包安装到 /opt/zlm_svac 并注册 systemd 服务
#
# 用法:
#   ./package/linux/install_linux.sh <zlm-svac_linux_*.tar.gz>
#   ./package/linux/install_linux.sh --uninstall      # 卸载并停止服务
#
# 需要 root 权限(非 root 自动加 sudo)
# ============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DIR="/opt/zlm_svac"

SUDO=""
[[ $EUID -eq 0 ]] || SUDO="sudo"

usage() {
    echo "用法: $0 <zlm-svac_linux_*.tar.gz>"
    echo "      $0 --uninstall"
    exit "${1:-0}"
}

# 卸载
if [[ "${1:-}" == "--uninstall" ]]; then
    echo "==> 停止并禁用服务..."
    $SUDO systemctl disable --now zlm-svac 2>/dev/null || true
    $SUDO rm -f /etc/systemd/system/zlm-svac.service
    $SUDO systemctl daemon-reload
    echo "==> 删除 $INSTALL_DIR ..."
    $SUDO rm -rf "$INSTALL_DIR"
    echo "已卸载 zlm-svac"
    exit 0
fi

TARBALL="${1:-}"
if [[ -z "$TARBALL" || ! -f "$TARBALL" ]]; then
    usage 1
fi
TARBALL="$(cd "$(dirname "$TARBALL")" && pwd)/$(basename "$TARBALL")"

echo "==> 解压 $TARBALL ..."
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
tar -xzf "$TARBALL" -C "$STAGE"

if [[ ! -x "$STAGE/zlm-svac/bin/MediaServer" ]]; then
    echo "[ERR] tar 包内未找到 zlm-svac/bin/MediaServer，包结构不正确"
    exit 1
fi

echo "==> 安装到 $INSTALL_DIR ..."
$SUDO rm -rf "$INSTALL_DIR"
$SUDO mkdir -p "$INSTALL_DIR"
$SUDO cp -a "$STAGE/zlm-svac/." "$INSTALL_DIR/"
$SUDO mkdir -p "$INSTALL_DIR/dump/disk1"
$SUDO chmod +x "$INSTALL_DIR/bin/MediaServer"

echo "==> 注册并启动 systemd 服务 ..."
$SUDO cp "$SCRIPT_DIR/zlm-svac.service" /etc/systemd/system/zlm-svac.service
$SUDO systemctl daemon-reload
$SUDO systemctl enable zlm-svac
$SUDO systemctl restart zlm-svac

echo ""
echo "安装完成。服务状态:"
$SUDO systemctl status zlm-svac --no-pager || true
echo ""
echo "常用命令:"
echo "  查看日志: journalctl -u zlm-svac -f"
echo "  停止/启动: systemctl stop|start zlm-svac"
echo "  配置修改: vi $INSTALL_DIR/conf/config.ini 后 systemctl restart zlm-svac"
