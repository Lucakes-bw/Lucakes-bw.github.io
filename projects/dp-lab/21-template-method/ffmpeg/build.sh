#!/bin/sh
# 《模板方法模式》：编译 FFmpeg 版的导出任务。macOS 用 Homebrew 的 FFmpeg；Linux 用 pkg-config
#（apt install libavformat-dev libavcodec-dev libswscale-dev libswresample-dev，编码 MP4 需要带 libx264 的 FFmpeg）
set -e
cd "$(dirname "$0")"
mkdir -p build
CXX=${CXX:-c++}
LIBS="libavformat libavcodec libswscale libswresample libavutil"
if [ "$(uname)" = Darwin ]; then
  FF=${FF:-/opt/homebrew/opt/ffmpeg}
  FFCFLAGS="-I$FF/include"; FFLIBS="-L$FF/lib -lavformat -lavcodec -lswscale -lswresample -lavutil"
else
  FFCFLAGS=$(pkg-config --cflags $LIBS)
  FFLIBS=$(pkg-config --libs $LIBS)
fi
$CXX -std=c++17 -O2 -Wall -Wextra $FFCFLAGS export.cpp -o build/export $FFLIBS
echo "编译完成：build/export"
