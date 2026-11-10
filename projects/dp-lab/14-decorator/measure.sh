#!/bin/sh
# 《装饰器模式》第 2 节的统计：继承和装饰器两种写法各有几个类、多少行，再加一种功能（丢掉坏包）时改了多少行
# 用法：sh measure.sh（在 14-decorator 目录下运行）。不算第一行注释和 main 函数（main 只是演示用的）
cd "$(dirname "$0")"
body() { sed -n '2,/^int main/p' "$1" | sed '$d'; }                  # 去掉第一行注释和 main
classes() { body "$1" | grep -cE '^class [A-Za-z]+ (:|\{)'; }
lines() { body "$1" | grep -cvE '^[[:space:]]*$'; }                  # 不算空行
changed() {                                                           # diff 里新增 / 删除的行数（不算空行）
    body "$1" > /tmp/_a.$$; body "$2" > /tmp/_b.$$
    add=$(diff /tmp/_a.$$ /tmp/_b.$$ | grep -cE '^>[[:space:]]*[^[:space:]]')
    del=$(diff /tmp/_a.$$ /tmp/_b.$$ | grep -cE '^<[[:space:]]*[^[:space:]]')
    rm -f /tmp/_a.$$ /tmp/_b.$$
    echo "+$add -$del"
}
for w in inherit decorator; do
    echo "== $w"
    for v in 1 2; do echo "  ${w}_v$v.cpp：$(classes ${w}_v$v.cpp) 个类，$(lines ${w}_v$v.cpp) 行"; done
    echo "  v1 → v2（加「丢掉坏包」）：$(changed ${w}_v1.cpp ${w}_v2.cpp)"
done
