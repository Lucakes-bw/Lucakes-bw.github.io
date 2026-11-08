#!/bin/sh
# 《单例》：分别用默认的符号可见性和 -fvisibility=hidden 编译两个动态库
set -e
cd "$(dirname "$0")"
CXX=${CXX:-clang++}
EXT=so; [ "$(uname)" = Darwin ] && EXT=dylib
for vis in default hidden; do
    rm -rf build && mkdir -p build
    $CXX -std=c++17 -fPIC -shared -fvisibility=$vis -o build/liba.$EXT lib_a.cpp
    $CXX -std=c++17 -fPIC -shared -fvisibility=$vis -o build/libb.$EXT lib_b.cpp
    $CXX -std=c++17 -o build/main main.cpp -Lbuild -la -lb -Wl,-rpath,"$(pwd)/build"
    echo "== -fvisibility=$vis"
    ./build/main | sed 's/0x[0-9a-f]*/0x…/g'
done
rm -rf build
