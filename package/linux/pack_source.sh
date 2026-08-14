#!/usr/bin/env bash
# ============================================================================
# ZLMediaKit SVAC fork — 源码打包脚本
# 把"干净源码"打成 tar.gz, 排除编译产物/录像/抓包/日志/IDE 文件,
# 用于拷贝到目标机编译, 或分发源码(不依赖 .gitignore, 任何拷贝方式都适用)
#
# 排除项:
#   编译产物  build/ release/ out/ X64/ linux/ cmake-build-*/
#   运行时数据 dump/(录像等) 抓包 *.pcapng *.pcap 日志 *.log ffmpeg/
#   git 元数据 .git 及子模块 .git
#   IDE .vs/ .vscode/ .idea/
#   打包产物 *.tar.gz *.zip
#
# 用法:
#   ./package/linux/pack_source.sh [-v VERSION]
#   -v VERSION 版本号(默认取 git describe, 失败则 src)
# 产物: zlm_svac_src_<version>.tar.gz (位于仓库根目录)
# ============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
cd "$REPO_ROOT"

VERSION=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        -v)
            [[ $# -ge 2 ]] || { echo "[ERR] -v 需要参数"; exit 1; }
            VERSION="$2"; shift 2 ;;
        -h|--help) echo "用法: $0 [-v VERSION]"; exit 0 ;;
        *) echo "[ERR] 未知参数: $1"; usage 1 ;;
    esac
done
[[ -n "$VERSION" ]] || VERSION="$(git describe --tags --always 2>/dev/null || echo src)"

DIRNAME="zlm_svac_src_${VERSION}"
OUT="${DIRNAME}.tar.gz"
[[ -f "$OUT" ]] && rm -f "$OUT"

echo "==> 打包源码 -> $OUT (排除编译产物/录像/抓包/日志/IDE 文件)"

# 在 Windows/活跃文件系统上, tar 可能因 "file changed as we read it" 返回非零(良性警告)
# 只要包已生成即视为成功
if ! tar -C "$REPO_ROOT" -czf "$OUT" \
    --transform="s,^\./,${DIRNAME}/," \
    --exclude='./.git' \
    --exclude='./.claude' \
    --exclude='./3rdpart/ZLToolKit/.git' \
    --exclude='./.gitmodules_github' \
    --exclude='./build' \
    --exclude='./release' \
    --exclude='./out' \
    --exclude='./dump' \
    --exclude='./X64' \
    --exclude='./linux' \
    --exclude='./cmake-build-*' \
    --exclude='./.vs' \
    --exclude='./.vscode' \
    --exclude='./.idea' \
    --exclude='./*.pcapng' \
    --exclude='./*.pcap' \
    --exclude='./*.log' \
    --exclude='./ffmpeg' \
    --exclude='./*.tar.gz' \
    --exclude='./*.zip' \
    . ; then
    if [[ -f "$OUT" ]]; then
        echo "[WARN] 打包过程中有文件被并发修改(良性), 包已生成"
    else
        echo "[ERR] tar 打包失败"
        exit 1
    fi
fi

echo "完成: $REPO_ROOT/$OUT"
echo ""
echo "源码包内容大小:"
tar -tzf "$OUT" | wc -l
echo "  (解压后需先 chmod +x package/linux/*.sh 再执行 build_linux.sh)"
