#!/bin/sh
# 《CRTP 奇异递归模板》：用 -O2 -Wall -Wextra 编译并运行本篇所有实验程序与压测；CXX=g++ sh measure.sh 换编译器
set -e
cd "$(dirname "$0")"
CXX=${CXX:-c++}
OUT=${OUT:-./build/measure}
mkdir -p "$OUT"

PROGS="virtual_filter crtp_filter crtp_mixin crtp_wrong_base_ce bench"
for f in $PROGS; do
  $CXX -std=c++17 -O2 -Wall -Wextra "$f.cpp" -o "$OUT/$f"
done

# 验证开启 TRIGGER_WRONG_BASE 时确实在编译期报错
echo "== crtp_wrong_base_ce (negative compile check)"
if $CXX -std=c++17 -O2 -DTRIGGER_WRONG_BASE -fsyntax-only crtp_wrong_base_ce.cpp 2>/dev/null; then
  echo "ERROR: expected compile error for wrong CRTP base!"
  exit 1
else
  echo "OK: wrong CRTP base rejected at compile time"
fi

for f in $PROGS; do
  echo "== $f"
  "$OUT/$f"
done
