#!/bin/sh
# 《访问者模式》：统计三种写法加一种操作、加一种元素各要改几处；漏改时编译器报几个错；再用 -O2 编译运行本篇的程序。CXX=g++ sh measure.sh 换编译器
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
echo "== 加一种操作（导出 SRT）"
stat methods.cpp methods_srt.cpp "虚函数      "
stat visitor.cpp visitor_srt.cpp "访问者      "
stat variant.cpp variant_srt.cpp "variant     "
echo "== 加一种元素（Underline）"
stat methods.cpp methods_underline.cpp "虚函数      "
stat visitor.cpp visitor_underline.cpp "访问者      "
stat variant.cpp variant_underline.cpp "variant     "

echo "== 加了 Underline 但各个操作都没改，编译器报几个错"
grep -v 'const Underline& u) override' visitor_underline.cpp > "$OUT/visitor_forgot.cpp"
grep -v 'operator()(const Underline& u)' variant_underline.cpp > "$OUT/variant_forgot.cpp"
n=$($CXX -std=c++17 -fsyntax-only "$OUT/visitor_forgot.cpp" 2>&1 | grep -c 'error:' || true)
echo "访问者：$n 个 error"
$CXX -std=c++17 -fsyntax-only "$OUT/visitor_forgot.cpp" 2>&1 | grep 'error:' | head -3
n=$($CXX -std=c++17 -fsyntax-only "$OUT/variant_forgot.cpp" 2>&1 | grep -c 'error:' || true)
echo "variant：$n 个 error"
$CXX -std=c++17 -fsyntax-only "$OUT/variant_forgot.cpp" 2>&1 | grep 'error:' | head -3
n=$($CXX -std=c++17 -Wall -Wextra -fsyntax-only dyncast.cpp 2>&1 | grep -c -E 'error:|warning:' || true)
echo "dynamic_cast：$n 个 error / warning"

PROGS="methods methods_srt methods_underline dyncast overload visitor visitor_srt visitor_underline variant variant_srt variant_underline merge bench ex2_width ex3_filters ex4_bug ex4_fixed"
for f in $PROGS; do
  $CXX -std=c++17 -O2 -Wall -Wextra $f.cpp -o "$OUT/$f"
done
for f in $PROGS; do
  echo "== $f"; "$OUT/$f"
done
