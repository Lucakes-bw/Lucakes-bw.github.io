#!/bin/sh
# 《装饰器》：编译 FFmpeg 的 AVIOContext 计数装饰器。macOS + Homebrew 的路径；Linux 上把两个路径换成 $(pkg-config --cflags --libs libavformat libavcodec libavutil)
set -e
cd "$(dirname "$0")"
FF=${FF:-/opt/homebrew/opt/ffmpeg}
mkdir -p build
CXX=${CXX:-clang++}
if [ -d "$FF/include" ]; then FLAGS="-I$FF/include -L$FF/lib -lavformat -lavcodec -lavutil"; else FLAGS=$(pkg-config --cflags --libs libavformat libavcodec libavutil); fi
$CXX -std=c++17 -O2 -g -Wall -Wextra avio_count.cpp -o build/avio_count $FLAGS
V=${1:-../../../../source/media/bbb-10s.mp4}
./build/avio_count "$V" 32768 4096          # 外层缓冲大
./build/avio_count "$V" 4096 32768          # 外层缓冲小
