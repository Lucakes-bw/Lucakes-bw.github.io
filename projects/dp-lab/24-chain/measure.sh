#!/bin/sh
# 《责任链模式》：统计加「播放列表」要改几处，再用 -O2 编译运行本篇的程序，最后看成环和悬空指针两个坑；CXX=g++ sh measure.sh 换编译器
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
  checks=$(grep -c -E 'if \((dialog|menu|list)Open\)' "$OUT/b.txt" || true)
  echo "$3：$hunks 处改动，删除/改写旧代码 $del 行，新增 $add 行；改完后 onKey 里判断面板开关的 if 有 $checks 个"
}
echo "== 加一个「播放列表」面板"
stat key_switch.cpp key_switch_playlist.cpp "一个 onKey 判断到底"
stat key_chain.cpp key_chain_playlist.cpp "责任链            "
grep -c -E 'if \((dialog|menu)Open\)' key_switch.cpp | sed 's/^/加之前 onKey 里判断面板开关的 if：/'

PROGS="key_switch key_switch_playlist key_chain key_chain_playlist log_chain chain_function middleware cycle bench ex2_annotate ex3_error_chain ex4_bug ex4_fixed"
for f in $PROGS; do
  $CXX -std=c++17 -O2 -Wall -Wextra $f.cpp -o "$OUT/$f"
done
for f in $PROGS; do
  echo "== $f"; "$OUT/$f"
done

echo "== cycle nocheck：链成环、不检查（3 秒超时）"
for o in -O0 -O2; do
  $CXX -std=c++17 $o cycle.cpp -o "$OUT/cycle$o"
  set +e; perl -e 'alarm 3; exec @ARGV' "$OUT/cycle$o" nocheck; rc=$?; set -e
  echo "   $o 退出码 $rc（139 = 段错误，142 = 超时被杀）"
done

echo "== dangling：-O2 直接运行，再用 -fsanitize=address"
$CXX -std=c++17 -O2 dangling.cpp -o "$OUT/dangling"
set +e; "$OUT/dangling"; echo "   退出码 $?"; set -e
$CXX -std=c++17 -O1 -g -fsanitize=address dangling.cpp -o "$OUT/dangling_asan"
"$OUT/dangling_asan" 2>&1 | grep -E 'ERROR|READ|#[0-9] .*dangling.cpp' | head -4 || true
