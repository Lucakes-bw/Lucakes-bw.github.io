#!/bin/sh
# 《观察者模式》：用 -O2 编译运行本篇的实测程序，再用 AddressSanitizer 跑几个有 bug 的版本；CXX=g++ sh measure.sh 换编译器
cd "$(dirname "$0")"
CXX=${CXX:-c++}
OUT=${OUT:-./build/measure}
mkdir -p "$OUT"
OK="hardcoded observer_basic iterate_detach reentrant order weak_observer signal push_pull event_bus bench_notify threaded ex2_fixed ex3_throttle ex4_fixed"
for f in $OK; do
  $CXX -std=c++17 -O2 -Wall -Wextra $f.cpp -o "$OUT/$f" -pthread || exit 1
done
for f in $OK; do
  echo "== $f"; "$OUT/$f"
done

echo "== 依赖：加一个弹幕模块"
CXX=$CXX sh deps/measure_deps.sh

# 有 bug 的版本：先不加 ASan 跑一次看现象，再加 ASan 看报告的第一行
for f in dangling iterate_detach iterate_attach ex4_bug; do
  $CXX -std=c++17 -O2 $f.cpp -o "$OUT/$f" 2>/dev/null
  echo "== $f（-O2，不加 ASan）"; "$OUT/$f"; echo "   退出码 $?"
  $CXX -std=c++17 -O1 -g -fsanitize=address -fno-omit-frame-pointer $f.cpp -o "$OUT/${f}_asan" 2>/dev/null
  echo "== $f（ASan）"; "$OUT/${f}_asan" 2>&1 | grep -E '^SUMMARY'
done
# libstdc++ 的 vector 默认不告诉 ASan 「size 之后、capacity 之内」的部分不能读，要加这个宏
$CXX -std=c++17 -O1 -g -fsanitize=address -D_GLIBCXX_SANITIZE_VECTOR iterate_detach.cpp -o "$OUT/iterate_detach_asan2" 2>/dev/null
echo "== iterate_detach（ASan + -D_GLIBCXX_SANITIZE_VECTOR）"; "$OUT/iterate_detach_asan2" 2>&1 | grep -E '^SUMMARY'
