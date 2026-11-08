#!/bin/sh
# 《SOLID 原则（下）》接口隔离实验：两个项目都先完整编译一次，再给 fax() 加一个参数，统计重新编译了哪些源文件
set -e
cd "$(dirname "$0")"
W0="$(pwd)/build"
for v in fat split; do
    W="$W0/$v"
    rm -rf "$W" && mkdir -p "$W0" && cp -r "$v" "$W"
    cmake -S "$W" -B "$W/build" -G "Unix Makefiles" >/dev/null
    cmake --build "$W/build" >/dev/null
    sleep 1
    # 需求变化：传真要支持重试次数。接口、所有实现、调用 fax 的地方都要改
    for f in $W/*.h $W/*.cpp; do
        sed -i.bak -e 's/const std::string& number)/const std::string\& number, int retries)/' \
                   -e 's/const std::string&)/const std::string\&, int)/' \
                   -e 's/"010-12345678")/"010-12345678", 3)/' "$f" && rm -f "$f.bak"
        # 内容没变的文件恢复原来的修改时间，只有真正改过的文件才会触发重新编译
        if cmp -s "$f" "$v/$(basename "$f")"; then touch -r "$v/$(basename "$f")" "$f"; fi
    done
    echo "== $v：改动的文件"
    for f in $W/*.h $W/*.cpp; do cmp -s "$f" "$v/$(basename "$f")" || echo "   $(basename "$f")"; done
    echo "== $v：重新编译的源文件"
    cmake --build "$W/build" -- -j1 2>&1 | grep "Building CXX" | sed 's/.*CMakeFiles\/office.dir\//   /; s/\.o$//'
    "$W/build/office" | tail -2
done
