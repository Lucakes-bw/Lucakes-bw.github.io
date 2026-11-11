#!/bin/sh
# 《观察者模式》：加一个「弹幕」模块，硬编码版和观察者版各要改几个文件、几行；Player 依赖几个模块；改弹幕的头文件要重新编译几个 .cpp
cd "$(dirname "$0")"
CXX=${CXX:-c++}
for v in coupled observer; do
  echo "== $v"
  changed=$(diff -rq $v ${v}_plus | grep -c '^Files')
  added=$(diff -rq $v ${v}_plus | grep -c '^Only in')
  lines=$(diff -r $v ${v}_plus | grep -c '^[<>]')
  echo "加弹幕：改动已有文件 $changed 个（$(diff -rq $v ${v}_plus | grep '^Files' | sed 's/.*_plus\/\([^ ]*\) differ/\1/' | tr '\n' ' ')），新增文件 $added 个，已有文件里增删 $lines 行"
  pl=$(diff -u $v/player.h ${v}_plus/player.h; diff -u $v/player.cpp ${v}_plus/player.cpp)
  echo "其中 Player 的两个文件增删 $(echo "$pl" | grep -c '^[-+][^-+]') 行"
  n1=$($CXX -std=c++17 -MM ${v}/player.cpp | tr ' \\' '\n\n' | grep -c -E 'progress_bar|subtitle|lyrics|stats|danmaku')
  n2=$($CXX -std=c++17 -MM ${v}_plus/player.cpp | tr ' \\' '\n\n' | grep -c -E 'progress_bar|subtitle|lyrics|stats|danmaku')
  echo "player.cpp 依赖的模块头文件：$n1 个，加弹幕后 $n2 个"
  r=0; t=0
  for f in ${v}_plus/*.cpp; do
    t=$((t+1))
    if $CXX -std=c++17 -MM $f | grep -q danmaku_view.h; then r=$((r+1)); echo "  改 danmaku_view.h 要重编：$(basename $f)"; fi
  done
  echo "改 danmaku_view.h：$t 个 .cpp 里要重编 $r 个"
done
