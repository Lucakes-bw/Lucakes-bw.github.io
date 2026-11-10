#!/bin/sh
# 《代理模式》：编译 FFmpeg 的虚代理示例。macOS 用 Homebrew 的 FFmpeg；Linux 用 pkg-config（apt install libavformat-dev libavcodec-dev libswscale-dev）
set -e
cd "$(dirname "$0")"
mkdir -p build
CXX=${CXX:-c++}
if [ "$(uname)" = Darwin ]; then
  FF=${FF:-/opt/homebrew/opt/ffmpeg}
  FFCFLAGS="-I$FF/include"; FFLIBS="-L$FF/lib -lavformat -lavcodec -lswscale -lavutil"
else
  FFCFLAGS=$(pkg-config --cflags libavformat libavcodec libswscale libavutil)
  FFLIBS=$(pkg-config --libs libavformat libavcodec libswscale libavutil)
fi
$CXX -std=c++17 -O2 -Wall -Wextra $FFCFLAGS lazy_strip.cpp -o build/lazy_strip $FFLIBS
$CXX -std=c++17 -O2 -Wall -Wextra $FFCFLAGS probe_cache.cpp -o build/probe_cache $FFLIBS
echo "编译完成：build/lazy_strip  build/probe_cache"
