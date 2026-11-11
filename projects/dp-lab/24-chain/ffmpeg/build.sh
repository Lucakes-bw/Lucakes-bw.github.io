#!/bin/sh
# 《责任链模式》：编译 FFmpeg 的解码器回退链。macOS 用 Homebrew 的 FFmpeg；Linux 用 pkg-config（apt install libavformat-dev libavcodec-dev libavutil-dev）
set -e
cd "$(dirname "$0")"
mkdir -p build
CXX=${CXX:-c++}
if [ "$(uname)" = Darwin ]; then
  FF=${FF:-/opt/homebrew/opt/ffmpeg}
  FFCFLAGS="-I$FF/include"; FFLIBS="-L$FF/lib -lavformat -lavcodec -lavutil"
else
  FFCFLAGS=$(pkg-config --cflags libavformat libavcodec libavutil)
  FFLIBS=$(pkg-config --libs libavformat libavcodec libavutil)
fi
$CXX -std=c++17 -O2 -Wall -Wextra $FFCFLAGS decoder_chain.cpp -o build/decoder_chain $FFLIBS
echo "编译完成：build/decoder_chain"
