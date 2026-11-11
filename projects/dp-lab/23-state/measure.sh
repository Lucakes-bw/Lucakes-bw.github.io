#!/bin/sh
# 《状态模式》：统计加「缓冲中」要改几处，看 -Wswitch 能抓到哪些漏改，再用 -O2 编译运行本篇的程序；CXX=g++ sh measure.sh 换编译器
set -e
cd "$(dirname "$0")"
CXX=${CXX:-c++}
OUT=${OUT:-./build/measure}
mkdir -p "$OUT"

stat() {   # 只比较「以下是测试」之前的部分，去掉第一行注释：几处改动（相邻的 hunk 算一处）、改写了几行旧代码、新增几行
  sed -n '2,/^\/\/ ---- 以下是测试/p' "$1" | sed '$d' > "$OUT/a.txt"; sed -n '2,/^\/\/ ---- 以下是测试/p' "$2" | sed '$d' > "$OUT/b.txt"
  d=$(diff -U0 "$OUT/a.txt" "$OUT/b.txt" || true)
  hunks=$(printf '%s\n' "$d" | awk '/^@@/ { split($2, r, ","); a = substr(r[1], 2) + 0; n = (r[2] == "") ? 1 : r[2] + 0;
           if (cnt == 0 || a > last + 1) cnt++; last = a + (n > 0 ? n - 1 : 0) } END { print cnt + 0 }')
  del=$(printf '%s\n' "$d" | grep -c '^-[^-]' || true)
  add=$(printf '%s\n' "$d" | grep -c '^+[^+]' || true)
  echo "$3：$hunks 处改动，删除/改写旧代码 $del 行，新增 $add 行"
}
echo "== 加一个「缓冲中」状态"
stat switch_player.cpp switch_player_buffering.cpp "到处判断 state_"
stat state_player.cpp state_player_buffering.cpp "状态模式      "

echo "== 只加枚举值、别的都不改，-Wall 报几个警告"
awk '/^enum class State/ { sub(/Paused, Ended/, "Paused, Buffering, Ended") } { print }' switch_player.cpp > "$OUT/only_enum.cpp"
$CXX -std=c++17 -Wall -Wextra -fsyntax-only "$OUT/only_enum.cpp" 2>&1 | grep -E 'warning' || echo "（没有警告）"

PROGS="switch_player switch_player_buffering state_player state_player_buffering table_player variant_player self_replace self_replace_fixed bench ex2_seeking ex3_download ex4_bug ex4_fixed"
for f in $PROGS; do
  $CXX -std=c++17 -O2 -Wall -Wextra $f.cpp -o "$OUT/$f"
done
for f in $PROGS; do
  echo "== $f"; "$OUT/$f"
done
echo "== self_replace（-fsanitize=address）"
$CXX -std=c++17 -O1 -g -fsanitize=address self_replace.cpp -o "$OUT/self_replace_asan"
"$OUT/self_replace_asan" 2>&1 | grep -E 'ERROR|READ|#[0-9] .*self_replace.cpp' | head -6 || true
