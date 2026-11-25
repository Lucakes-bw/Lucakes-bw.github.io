#!/bin/sh
# 《类型擦除》：用 -O2 -Wall -Wextra 编译并运行本篇所有实验程序与压测；CXX=g++ sh measure.sh 换编译器
set -e
cd "$(dirname "$0")"
CXX=${CXX:-c++}
OUT=${OUT:-./build/measure}
mkdir -p "$OUT"

PROGS="inheritance_pain type_erasure_basic sbo_type_erasure bench"
for f in $PROGS; do
  $CXX -std=c++17 -O2 -Wall -Wextra "$f.cpp" -o "$OUT/$f"
done

for f in $PROGS; do
  echo "== $f"
  "$OUT/$f"
done
