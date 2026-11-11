#!/bin/sh
# 《迭代器模式》：把环形缓冲区的内部改成「2 的幂 + 计数器」，统计类内、调用方各要改几处；CXX=g++ sh measure.sh 换编译器
set -e
cd "$(dirname "$0")"
CXX=${CXX:-c++}
OUT=${OUT:-./build/measure}
mkdir -p "$OUT"

part() {   # $1 文件 $2 class|caller：「===== 调用方 =====」之前是类，之后到 main 之前是调用方（去掉第一行注释）
  if [ "$2" = class ]; then tail -n +2 "$1" | sed '/===== 调用方 =====/,$d'
  else sed -n '/===== 调用方 =====/,/^int main/p' "$1" | sed '1d;$d'; fi
}
stat() {   # 几处改动（相邻的 hunk 算一处）、改写旧代码几行、新增几行
  part "$1" "$3" > "$OUT/a.txt"; part "$2" "$3" > "$OUT/b.txt"
  d=$(diff -U0 "$OUT/a.txt" "$OUT/b.txt" || true)
  hunks=$(printf '%s\n' "$d" | awk '/^@@/ { split($2, r, ","); a = substr(r[1], 2) + 0; n = (r[2] == "") ? 1 : r[2] + 0;
           if (cnt == 0 || a > last + 1) cnt++; last = a + (n > 0 ? n - 1 : 0) } END { print cnt + 0 }')
  del=$(printf '%s\n' "$d" | grep -c '^-[^-]' || true)
  add=$(printf '%s\n' "$d" | grep -c '^+[^+]' || true)
  printf '  %-6s %d 处改动，改写旧代码 %d 行，新增 %d 行\n' "$3" "$hunks" "$del" "$add"
}
echo "== 调用方里自己算下标的地方（出现 data()[ 的行）"
echo "  暴露内部版：$(part exposed.cpp caller | grep -c 'data()\[') 处；迭代器版：$(part ring_iter.cpp caller | grep -c 'data()\[' || true) 处"
echo "== 暴露内部版：exposed.cpp → exposed_v2.cpp"
stat exposed.cpp exposed_v2.cpp class
stat exposed.cpp exposed_v2.cpp caller
echo "== 迭代器版：ring_iter.cpp → ring_iter_v2.cpp"
stat ring_iter.cpp ring_iter_v2.cpp class
stat ring_iter.cpp ring_iter_v2.cpp caller

echo "== 用 -O2 编译运行，四个程序的输出"
for p in exposed exposed_v2 ring_iter ring_iter_v2; do
  $CXX -std=c++17 -O2 -Wall -Wextra $p.cpp -o "$OUT/$p"
  echo "-- $p"; "$OUT/$p"
done
