#!/bin/sh
# Linux(llvm-mingw)에서 크로스 빌드 -> ../FrameBooster.exe
set -e
TC=${TC:-/opt/llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64/bin}
cd "$(dirname "$0")/../src"
$TC/x86_64-w64-mingw32-windres app.rc -O coff -o app.res.o
$TC/x86_64-w64-mingw32-clang++ -std=c++17 -Os -Wall -Wextra -Wno-missing-field-initializers -ffunction-sections -fdata-sections -Wl,--gc-sections \
  -municode -mwindows main.cpp app.res.o -o ../FrameBooster.exe -static -s \
  -ld2d1 -ldwrite -ldxgi -lpowrprof -lwinhttp -lbcrypt -ldwmapi -lpsapi -lversion -lole32 -lshell32 -ladvapi32 -lgdi32 -luser32
rm -f app.res.o
