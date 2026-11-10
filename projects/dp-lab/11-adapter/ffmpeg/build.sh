#!/bin/sh
# 《适配器》：编译 FFmpeg 适配器的示例。macOS + Homebrew 的路径；Linux 上把两个路径换成 $(pkg-config --cflags --libs libavformat libavutil)
set -e
cd "$(dirname "$0")"
FF=${FF:-/opt/homebrew/opt/ffmpeg}
mkdir -p build
CXX=${CXX:-clang++}
$CXX -std=c++17 -O2 -g -I"$FF/include" main.cpp ffmpeg_source.cpp -o build/demo -L"$FF/lib" -lavformat -lavcodec -lavutil
$CXX -std=c++17 -O2 -g -I"$FF/include" raw_c.cpp -o build/raw_c -L"$FF/lib" -lavformat -lavcodec -lavutil
$CXX -std=c++17 -O2 -g -I"$FF/include" early_stop.cpp ffmpeg_source.cpp -o build/early_stop -L"$FF/lib" -lavformat -lavcodec -lavutil
