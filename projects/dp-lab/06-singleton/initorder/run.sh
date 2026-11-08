#!/bin/sh
# 《单例》静态初始化顺序：同样三个源文件，只调换链接顺序
set -e
cd "$(dirname "$0")"
CXX=${CXX:-clang++}
mkdir -p build
$CXX -std=c++17 -o build/a main.cpp config.cpp logger.cpp
$CXX -std=c++17 -o build/b main.cpp logger.cpp config.cpp
echo "== 链接顺序：main.cpp config.cpp logger.cpp"; ./build/a
echo "== 链接顺序：main.cpp logger.cpp config.cpp"; ./build/b
