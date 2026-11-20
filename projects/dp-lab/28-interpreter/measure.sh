#!/bin/sh
# 《解释器模式》：用 -O2 -Wall -Wextra 编译并运行本篇所有实验与压测程序。CXX=g++ sh measure.sh 换编译器
set -e
cd "$(dirname "$0")"
CXX=${CXX:-c++}
OUT=${OUT:-./build/measure}
mkdir -p "$OUT"

PROGS="hardcoded_filter interpreter_basic variant_ast bench ex2_in_expr ex3_short_circuit"
for f in $PROGS; do
  $CXX -std=c++17 -O2 -Wall -Wextra "$f.cpp" -o "$OUT/$f"
done
for f in $PROGS; do
  echo "== $f"
  "$OUT/$f"
done
