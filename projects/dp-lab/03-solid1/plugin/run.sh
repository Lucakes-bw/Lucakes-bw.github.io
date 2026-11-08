#!/bin/sh
# 《SOLID 原则（上）》插件实验：先编译主程序和两个插件，运行；再只编译第三个插件，运行，比较主程序的 md5
set -e
cd "$(dirname "$0")"
CXX=${CXX:-clang++}
rm -rf build && mkdir -p build/plugins
$CXX -std=c++17 -o build/validator validator.cpp
for r in 10_min_length 20_has_digit; do $CXX -std=c++17 -shared -fPIC -o build/plugins/$r.so rules/$r.cpp; done
cd build
echo "== 两个插件"; md5sum validator 2>/dev/null || md5 -r validator
./validator "my pass"
echo "== 新增插件 30_no_space.so，主程序不重新编译"
$CXX -std=c++17 -shared -fPIC -o plugins/30_no_space.so ../rules/30_no_space.cpp
md5sum validator 2>/dev/null || md5 -r validator
./validator "my pass"
