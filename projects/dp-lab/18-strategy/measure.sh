#!/bin/sh
# 《策略模式》：统计加一种算法要改几处，再用 -O2 编译运行本篇的实测程序；CXX=g++ sh measure.sh 换编译器
#   FRAME=frame.pgm sh measure.sh 指定测试帧；不指定时用 ffmpeg 从 bbb-10s.mp4 第 5 秒取一帧（灰度）
set -e
cd "$(dirname "$0")"
CXX=${CXX:-c++}
OUT=${OUT:-./build/measure}
mkdir -p "$OUT"

stat() {   # 去掉第一行注释再比较：几处改动（hunk）、改了几行旧代码、新增几行
  tail -n +2 "$1" > "$OUT/a.txt"; tail -n +2 "$2" > "$OUT/b.txt"
  d=$(diff -U0 "$OUT/a.txt" "$OUT/b.txt" || true)
  # 几处改动：相邻的 hunk 算一处（GNU diff 和 BSD diff 切分 hunk 的方式不一样）
  hunks=$(printf '%s\n' "$d" | awk '/^@@/ { split($2, r, ","); a = substr(r[1], 2) + 0; n = (r[2] == "") ? 1 : r[2] + 0;
           if (cnt == 0 || a > last + 1) cnt++; last = a + (n > 0 ? n - 1 : 0) } END { print cnt + 0 }')
  del=$(printf '%s\n' "$d" | grep -c '^-[^-]' || true)
  add=$(printf '%s\n' "$d" | grep -c '^+[^+]' || true)
  echo "$3：$hunks 处改动，删除/改写旧代码 $del 行，新增 $add 行"
}
echo "== 加一种 Lanczos 算法"
stat switch_scaler.cpp switch_scaler_lanczos.cpp "enum + switch"
stat strategy_scaler.cpp strategy_scaler_lanczos.cpp "策略      "

PROGS="switch_scaler strategy_scaler modern runtime_switch bench_granularity sizes quality ex2_area ex3_rate_control ex4_bug ex4_fixed"
for f in $PROGS; do
  $CXX -std=c++17 -O2 -Wall -Wextra $f.cpp -o "$OUT/$f"
done
for f in switch_scaler strategy_scaler modern runtime_switch bench_granularity sizes ex2_area ex3_rate_control ex4_bug ex4_fixed; do
  echo "== $f"; "$OUT/$f"
done

if [ -z "$FRAME" ]; then
  FRAME="$OUT/frame.pgm"
  ffmpeg -v error -y -ss 5 -i ../../../source/media/bbb-10s.mp4 -frames:v 1 -pix_fmt gray "$FRAME"
fi
echo "== quality"; "$OUT/quality" "$FRAME"
