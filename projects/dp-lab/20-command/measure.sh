#!/bin/sh
# 《命令模式》：用 -O2 编译运行本篇的实测程序，再用 AddressSanitizer 跑悬空捕获的版本；CXX=g++ sh measure.sh 换编译器
cd "$(dirname "$0")"
CXX=${CXX:-c++}
OUT=${OUT:-./build/measure}
mkdir -p "$OUT"
OK="direct_edit command_basic macro_merge bench_undo modern bench_forms job_queue journal ex2_trim ex3_limit ex4_bug ex4_fixed"
for f in $OK; do
  $CXX -std=c++17 -O2 -Wall -Wextra $f.cpp -o "$OUT/$f" -pthread || exit 1
done
for f in $OK; do
  echo "== $f"; "$OUT/$f"
done

# 有 bug 的版本：先不加 ASan 跑一次看现象，再加 ASan 看报告的第一行
f=dangling_capture
$CXX -std=c++17 -O2 $f.cpp -o "$OUT/$f" 2>/dev/null
echo "== $f（-O2，不加 ASan）"; "$OUT/$f"; echo "   退出码 $?"
$CXX -std=c++17 -O1 -g -fsanitize=address -fno-omit-frame-pointer $f.cpp -o "$OUT/${f}_asan" 2>/dev/null
echo "== $f（ASan）"; "$OUT/${f}_asan" 2>&1 | grep -E '^SUMMARY'
