#!/usr/bin/env bash
# 生成棋盘形状的大数据：N 条横线、N 条纵线，每个交叉点都是换乘站。
# 用法：tests/m2/data/gen-grid.sh 100 > tests/m2/data/grid.txt
N=${1:-100}
awk -v N="$N" '
function w(i, j, k,   x) {               # 1～9 分钟，看起来没有规律，减少用时相同的路线
    x = (i * 7919 + j * 104729 + k * 1299709) % 1000003
    return 1 + (x * x) % 9973 % 9
}
BEGIN {
    print "# " N "×" N " 的棋盘：横线 H0～H" N-1 "，纵线 V0～V" N-1 "，车站 S行-列"
    for (i = 0; i < N; i++) {
        print "LINE H" i
        for (j = 0; j < N; j++) print "S" i "-" j (j ? " " w(i, j, 1) : "")
        print "END"
    }
    for (j = 0; j < N; j++) {
        print "LINE V" j
        for (i = 0; i < N; i++) print "S" i "-" j (i ? " " w(i, j, 2) : "")
        print "END"
    }
}'
