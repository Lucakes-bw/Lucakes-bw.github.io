#!/bin/sh
# 《中介者模式》：统计加「只导出片段」面板要改几处，再用 -O2 编译运行本篇的程序，最后看重入死循环和悬空同事两个坑；CXX=g++ sh measure.sh 换编译器
set -e
cd "$(dirname "$0")"
CXX=${CXX:-c++}
OUT=${OUT:-./build/measure}
mkdir -p "$OUT"

stat() {   # 只比较「以下是测试」之前的部分，去掉第一行注释：几处改动（相邻的 hunk 算一处）、删除/改写旧代码几行、新增几行
  sed -n '2,/^\/\/ ---- 以下是测试/p' "$1" | sed '$d' > "$OUT/a.txt"
  sed -n '2,/^\/\/ ---- 以下是测试/p' "$2" | sed '$d' > "$OUT/b.txt"
  d=$(diff -U0 "$OUT/a.txt" "$OUT/b.txt" || true)
  hunks=$(printf '%s\n' "$d" | awk '/^@@/ { split($2, r, ","); a = substr(r[1], 2) + 0; n = (r[2] == "") ? 1 : r[2] + 0;
           if (cnt == 0 || a > last + 1) cnt++; last = a + (n > 0 ? n - 1 : 0) } END { print cnt + 0 }')
  del=$(printf '%s\n' "$d" | grep -c '^-[^-]' || true)
  add=$(printf '%s\n' "$d" | grep -c '^+[^+]' || true)
  peers=$(grep -c -E '(FormatSelect|VideoPanel|AudioPanel|RangePanel|ExportButton)\*' "$OUT/b.txt" || true)
  echo "$3：$hunks 处改动，删除/改写旧代码 $del 行，新增 $add 行；控件之间直接持有的邻居指针 $peers 个"
}

echo "== 加一个「只导出片段」面板（RangePanel）"
stat mesh_dialog.cpp mesh_dialog_range.cpp         "网状互指"
stat mediator_dialog.cpp mediator_dialog_range.cpp "中介者  "

PROGS="mesh_dialog mesh_dialog_range mediator_dialog mediator_dialog_range pingpong callback_mediator bench ex2_preset ex3_room ex4_fixed"
for f in $PROGS; do
  $CXX -std=c++17 -O2 -Wall -Wextra $f.cpp -o "$OUT/$f"
done
for f in $PROGS; do
  echo "== $f"; "$OUT/$f"
done

echo "== pingpong nbl：无重入保护时来回触发直到栈溢出"
$CXX -std=c++17 -O0 pingpong.cpp -o "$OUT/pingpong_o0"
set +e; ( "$OUT/pingpong_o0" nbl ) >/dev/null 2>&1; rc=$?; set -e
echo "   -O0 退出码 ${rc}（139 = 段错误/栈溢出）"

echo "== dangling_colleague 与 ex4_bug：用 -fsanitize=address 检查悬空同事"
for f in dangling_colleague ex4_bug; do
  $CXX -std=c++17 -O1 -g -fsanitize=address -fno-omit-frame-pointer $f.cpp -o "$OUT/${f}_asan"
  echo "== $f (ASan)"
  "$OUT/${f}_asan" 2>&1 | grep -E 'ERROR|READ|SUMMARY|#[0-9] .*(dangling_colleague|ex4_bug)\.cpp' | head -6 || true
done
