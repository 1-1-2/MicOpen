#!/usr/bin/env bash
# Optional monitor test modules; source and licenses remain alongside the host.
set -euo pipefail
cd "$(dirname "$0")/.."
CXX=${CXX:-x86_64-w64-mingw32-g++}
CC=${CC:-x86_64-w64-mingw32-gcc}
mkdir -p build
"$CXX" -std=c++17 -Os -s -shared -static -static-libgcc -static-libstdc++ \
  -I monitor/vendor/ringout -o build/miclink-ringout.dll \
  monitor/ringout.cpp monitor/vendor/ringout/RingOutDSP.cpp -Wl,--out-implib,build/ringout.a
if [ ! -f deps/speex/lib/libspeexdsp.a ]; then
  work=$(mktemp -d);trap 'rm -rf "$work"' EXIT
  curl --fail --location --retry 2 --max-time 120 \
    https://downloads.xiph.org/releases/speex/speexdsp-1.2.1.tar.gz -o "$work/source.tar.gz"
  echo '8c777343e4a6399569c72abc38a95b24db56882c83dbdb6c6424a5f4aeb54d3d  '"$work/source.tar.gz" | sha256sum -c -
  tar --no-same-owner -xzf "$work/source.tar.gz" -C "$work"
  dest="$PWD/deps/speex";mkdir -p "$dest"
  (cd "$work/speexdsp-1.2.1" && ./configure --host=x86_64-w64-mingw32 --prefix="$dest" \
    --disable-shared --enable-static --disable-examples --disable-sse --disable-sse2 CFLAGS='-Os' >configure.log 2>&1 && make -j4 >make.log 2>&1 && make install >install.log 2>&1) || {
      tail -40 "$work/speexdsp-1.2.1/"*.log;exit 1; }
  cp "$work/speexdsp-1.2.1/COPYING" "$dest/COPYING"
  cp "$work/source.tar.gz" "$dest/source.tar.gz"
fi
"$CC" -std=c99 -Os -s -shared -static-libgcc -Ideps/speex/include \
  monitor/speex.c deps/speex/lib/libspeexdsp.a -lm -o build/miclink-speex.dll
# GPL test combination: keep full corresponding sources in the distributed zip.
cp monitor/vendor/ringout/LICENSE build/RingOut-GPL-3.0.txt
cp deps/speex/COPYING build/SpeexDSP-COPYING.txt
for file in build/miclink-{ringout,speex}.dll; do
  if x86_64-w64-mingw32-objdump -p "$file" | sed -n 's/.*DLL Name: //p' | grep -iE 'libgcc|libwinpthread|libstdc|vcruntime|msvcp'; then
    echo "Unexpected runtime dependency: $file" >&2;exit 1
  fi
  stat -c '%n: %s bytes' "$file"
done
"$CXX" -std=c++17 -Os -s -static -o build/test_monitor_dsp.exe monitor/test_dsp.cpp
"$CC" -Os -s -o build/test_monitor_host.exe -x c++ monitor/test_host.cpp -x none build/monitor.o -lole32 -luuid

cp deps/speex/source.tar.gz build/speexdsp-1.2.1.tar.gz
python3 monitor/package_source.py
