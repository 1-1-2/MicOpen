#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
"${CC:-x86_64-w64-mingw32-gcc}" -Os -s -std=c99 -Wall -D_WIN32_WINNT=0x0A00 \
  -DMICLINK_ADB_SHARED_PORT=50370 tools/adb_lifecycle_probe.c src/transport.c \
  -lws2_32 -lbthprops -o build/test_adb_lifecycle.exe
"${CC:-x86_64-w64-mingw32-gcc}" -Os -s -std=c99 -Wall -D_WIN32_WINNT=0x0A00 \
  tools/fake_adb.c -lws2_32 -o build/test_adb_fixture.exe
