#!/bin/sh
# 《模板方法模式》：统计重复的代码和加需求要改几处，再用 -O2 编译运行本篇的程序；CXX=g++ sh measure.sh 换编译器
set -e
cd "$(dirname "$0")"
CXX=${CXX:-c++}
OUT=${OUT:-./build/measure}
mkdir -p "$OUT"

body() {   # 取出函数体（从「bool 函数名(」到下一个顶格的「}」），去掉首尾空白
  awk -v f="$2" '$0 ~ "^bool " f "\\(" { on = 1; next } on && /^}/ { on = 0 } on { gsub(/^ +| +$/, ""); print }' "$1"
}
echo "== 复制粘贴版：三个导出函数里的重复"
body copy_paste.cpp exportMp4 > "$OUT/mp4.txt"
body copy_paste.cpp exportGif > "$OUT/gif.txt"
body copy_paste.cpp exportWav > "$OUT/wav.txt"
for f in mp4 gif wav; do
  n=$(wc -l < "$OUT/$f.txt" | tr -d ' ')
  echo "  export$f：$n 行"
done
# 三个函数里都出现、而且完全相同的行（同一行出现几次就算几次）
common=$(awk 'FILENAME == ARGV[1] { a[$0]++; next } FILENAME == ARGV[2] { b[$0]++; next }
              { c[$0]++ } END { for (k in c) { m = c[k]; if (a[k] < m) m = a[k]; if (b[k] < m) m = b[k]; s += m } print s + 0 }' \
         "$OUT/gif.txt" "$OUT/wav.txt" "$OUT/mp4.txt")
total=$(cat "$OUT/mp4.txt" "$OUT/gif.txt" "$OUT/wav.txt" | wc -l | tr -d ' ')
echo "  三个函数共 $total 行，其中每个函数都有、一字不差的 $common 行（×3 = $((common * 3)) 行）"

stat() {   # 去掉第一行注释和 main 函数再比较：几处改动（相邻的 hunk 算一处）、改写旧代码几行、新增几行
  tail -n +2 "$1" | sed '/^int main/,$d' > "$OUT/a.txt"; tail -n +2 "$2" | sed '/^int main/,$d' > "$OUT/b.txt"
  d=$(diff -U0 "$OUT/a.txt" "$OUT/b.txt" || true)
  hunks=$(printf '%s\n' "$d" | awk '/^@@/ { split($2, r, ","); a = substr(r[1], 2) + 0; n = (r[2] == "") ? 1 : r[2] + 0;
           if (cnt == 0 || a > last + 1) cnt++; last = a + (n > 0 ? n - 1 : 0) } END { print cnt + 0 }')
  del=$(printf '%s\n' "$d" | grep -c '^-[^-]' || true)
  add=$(printf '%s\n' "$d" | grep -c '^+[^+]' || true)
  echo "  $3：$hunks 处改动，改写旧代码 $del 行，新增 $add 行"
}
echo "== 加进度回调和取消（不算 main 里的调用）"
stat copy_paste.cpp copy_paste_progress.cpp "复制粘贴版"
stat template_method.cpp template_method_progress.cpp "模板方法版"

PROGS="copy_paste copy_paste_progress template_method template_method_progress call_super ctor_virtual
       streambuf_nvi crtp function_steps bench_hooks ex2_cover ex3_fixture ex4_bug"
for f in $PROGS; do
  $CXX -std=c++17 -O2 -Wall -Wextra $f.cpp -o "$OUT/$f"
done
for f in $PROGS; do
  echo "== $f"; "$OUT/$f"
done
echo "== pure_call（会异常终止）"
$CXX -std=c++17 -O2 -Wall -Wextra pure_call.cpp -o "$OUT/pure_call"
"$OUT/pure_call" || echo "  退出码 $?"
echo "== pure_direct_ce.cpp（直接在构造函数里调纯虚函数：看警告和链接错误）"
$CXX -std=c++17 -Wall -Wextra pure_direct_ce.cpp -o "$OUT/pure_direct" 2>&1 | grep -E 'warning|error|undefined' || true
