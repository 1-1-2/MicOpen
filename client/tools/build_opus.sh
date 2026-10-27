#!/usr/bin/env bash
# 交叉编译 libopus（静态、仅 decoder 会被链接进来）到 client/deps/opus/
#
# 只在首次构建或换 opus 版本时需要跑。产物已落在 deps/opus 后，build.sh 完全离线。
# 体积控制：--enable-fixed-point（不需要浮点 API）、--disable-rtcd（不要运行时
# CPU 派发）、-Os、只编库不编工具。build.sh 链接时用 -Wl,--gc-sections 把
# encoder 与未引用的 object 全部裁掉，最终只留下 decoder 相关代码（约 140 KB）。
set -euo pipefail

VER=${OPUS_VER:-1.5.2}
DEST="$(cd "$(dirname "$0")/.." && pwd)/deps/opus"
HOST=${HOST:-x86_64-w64-mingw32}
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}

# WORK、RUNNER_TEMP 或 TMPDIR 可指定临时目录根；默认由 mktemp 选择系统目录。
# 每次使用独立子目录，避免固定路径互相覆盖，也不清理调用方已有目录。
TMP_ROOT="${WORK:-${RUNNER_TEMP:-${TMPDIR:-}}}"
if [ -n "$TMP_ROOT" ]; then
    mkdir -p "$TMP_ROOT"
    WORK_DIR="$(mktemp -d "$TMP_ROOT/miclink-opus-build.XXXXXX")"
else
    WORK_DIR="$(mktemp -d)"
fi

command -v "${HOST}-gcc" >/dev/null 2>&1 || {
    echo "缺少 ${HOST}-gcc，请先 apt install gcc-mingw-w64-x86-64" >&2
    exit 1
}

mkdir -p "$WORK_DIR"
cd "$WORK_DIR"

echo "==> 下载 opus-${VER}"
curl -sSL -o opus.tar.gz "https://downloads.xiph.org/releases/opus/opus-${VER}.tar.gz"
tar xzf opus.tar.gz
cd "opus-${VER}"

echo "==> configure"
./configure \
    --host="${HOST}" \
    --disable-shared \
    --enable-static \
    --disable-extra-programs \
    --disable-doc \
    --disable-stack-protector \
    --enable-fixed-point \
    --disable-rtcd \
    CFLAGS="-Os -DOPUS_BUILD -DNDEBUG" >configure.log 2>&1 || {
        tail -30 configure.log >&2
        exit 1
    }

echo "==> make"
make -j"$JOBS" >make.log 2>&1 || { tail -40 make.log >&2; exit 1; }

echo "==> 安装到 $DEST"
rm -rf "$DEST"
mkdir -p "$DEST/include/opus" "$DEST/lib"
# 1.5.x 的头文件是平的（include/opus.h），更早版本在 include/opus/ 下；两种都兼容。
cp include/opus*.h "$DEST/include/"
cp include/opus*.h "$DEST/include/opus/"
cp .libs/libopus.a "$DEST/lib/"
# 公开二进制随包保留所链接版本的原始许可，不能只给一个上游链接。
cp COPYING "$DEST/COPYING"
printf '%s\n' "$VER" > "$DEST/VERSION"

echo "完成："
ls -l "$DEST/lib/libopus.a"
ls "$DEST/include" "$DEST/include/opus"
