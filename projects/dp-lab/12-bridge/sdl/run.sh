#!/bin/sh
# 《桥接模式》：同一段绘制代码跑在 SDL 的每个渲染驱动上。需要 SDL3（macOS：brew install sdl3）
# Linux 上把路径换成 $(pkg-config --cflags --libs sdl3)；没有显示器时可以加 SDL_VIDEO_DRIVER=dummy
set -e
cd "$(dirname "$0")"
SDL=${SDL:-/opt/homebrew/opt/sdl3}
mkdir -p build
${CXX:-clang++} -std=c++17 -O2 -I"$SDL/include" sdl_drivers.cpp -o build/sdl_drivers -L"$SDL/lib" -lSDL3 -Wl,-rpath,"$SDL/lib"
./build/sdl_drivers
