#!/bin/sh
# 《代理模式》：用 -O2 编译并运行本篇的实测程序（不依赖 FFmpeg 的部分）；CXX=g++ sh measure.sh 换编译器
set -e
cd "$(dirname "$0")"
CXX=${CXX:-c++}
OUT=${OUT:-./build/measure}
mkdir -p "$OUT"
for f in no_proxy virtual_proxy bench_lazy protection_proxy cache_proxy remote_proxy smart_ptr_proxy vector_bool lazy_template ex2_unload ex3_order ex4_bug ex4_fixed; do
  $CXX -std=c++17 -O2 -Wall -Wextra $f.cpp -o "$OUT/$f"
done
for f in no_proxy virtual_proxy bench_lazy protection_proxy cache_proxy remote_proxy smart_ptr_proxy vector_bool lazy_template ex2_unload ex3_order ex4_bug ex4_fixed; do
  echo "== $f"; "$OUT/$f"
done
