#!/bin/sh
# 《工厂方法》：从测试视频提取样本，用三种方式构建并运行
#   1. 直接链接所有 .o 文件
#   2. 把解析器打包成静态库 libparsers.a 再链接
#   3. 静态库 + 强制链接整个库（Linux：--whole-archive，macOS：-force_load）
set -e
cd "$(dirname "$0")"
CXX=${CXX:-clang++}
MEDIA=${MEDIA:-../../../../source/media/bbb-10s.mp4}
rm -f build/*.o build/*.a build/app* && mkdir -p build/samples      # 保留已经提取好的样本
if [ -f "$MEDIA" ] && command -v ffmpeg >/dev/null && [ ! -f build/samples/bbb.aac ]; then
    ffmpeg -v error -i "$MEDIA" -c copy -t 2 build/samples/bbb.mp4
    ffmpeg -v error -i "$MEDIA" -an -c:v copy -bsf:v h264_mp4toannexb -f h264 build/samples/bbb.h264
    ffmpeg -v error -i "$MEDIA" -vn -c:a copy -f adts build/samples/bbb.aac
fi
SAMPLES=$(ls build/samples/* 2>/dev/null || true)
for f in registry mp4_parser h264_parser aac_parser main; do $CXX -std=c++17 -O2 -c $f.cpp -o build/$f.o; done
ar rcs build/libparsers.a build/mp4_parser.o build/h264_parser.o build/aac_parser.o

echo "== 1. 直接链接所有 .o"
$CXX -o build/app1 build/main.o build/registry.o build/mp4_parser.o build/h264_parser.o build/aac_parser.o
./build/app1 $SAMPLES

echo "== 2. 链接静态库 libparsers.a"
$CXX -o build/app2 build/main.o build/registry.o build/libparsers.a
./build/app2 $SAMPLES

echo "== 3. 静态库 + 强制链接整个库"
if [ "$(uname)" = Darwin ]; then
    $CXX -o build/app3 build/main.o build/registry.o -Wl,-force_load,build/libparsers.a
else
    $CXX -o build/app3 build/main.o build/registry.o -Wl,--whole-archive build/libparsers.a -Wl,--no-whole-archive
fi
./build/app3 $SAMPLES
