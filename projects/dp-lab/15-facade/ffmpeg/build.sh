#!/bin/sh
# 《外观模式》：编译 FFmpeg 缩略图的示例。macOS 用 Homebrew 的 FFmpeg；Linux 用 pkg-config（apt install libavformat-dev libavcodec-dev libswscale-dev）
# 外观编译成动态库 libthumbnailer，调用方（main、sheet）只链接它，不直接链接 FFmpeg
set -e
cd "$(dirname "$0")"
mkdir -p build
CXX=${CXX:-c++}
LIBS="-lavformat -lavcodec -lswscale -lavutil"
if [ "$(uname)" = Darwin ]; then
  FF=${FF:-/opt/homebrew/opt/ffmpeg}
  FFCFLAGS="-I$FF/include"; FFLIBS="-L$FF/lib $LIBS"
  SO=build/libthumbnailer.dylib; SHARED="-dynamiclib -install_name @rpath/libthumbnailer.dylib"; RPATH="-Wl,-rpath,@loader_path"
else
  FFCFLAGS=$(pkg-config --cflags libavformat libavcodec libswscale libavutil)
  FFLIBS=$(pkg-config --libs libavformat libavcodec libswscale libavutil)
  SO=build/libthumbnailer.so; SHARED="-shared"; RPATH='-Wl,-rpath,$ORIGIN'
fi
OPT="-std=c++17 -O2 -g -Wall -Wextra"
# 不用外观：调用方自己 include FFmpeg 的头文件、链接 4 个 FFmpeg 库
$CXX $OPT $FFCFLAGS raw_thumb.cpp -o build/raw_thumb $FFLIBS
# 外观：只有 thumbnailer.cpp 认识 FFmpeg
$CXX $OPT -fPIC $FFCFLAGS $SHARED thumbnailer.cpp -o $SO $FFLIBS
$CXX $OPT main.cpp -o build/thumb -Lbuild -lthumbnailer "$RPATH"
$CXX $OPT sheet.cpp -o build/sheet -Lbuild -lthumbnailer "$RPATH"
# 测开销：三种写法链接进同一个程序
$CXX $OPT -DNO_MAIN $FFCFLAGS bench.cpp thumbnailer.cpp raw_thumb.cpp -o build/bench $FFLIBS
echo "编译完成：build/raw_thumb  build/thumb  build/sheet  build/bench"
