#!/bin/sh
# 《Pimpl 惯用法》：编译并运行全部实验程序、验证不完整类型编译错误、实测增量编译耗时与运行期基准
# 用法：sh measure.sh（CXX=g++ sh measure.sh 可切换编译器）
set -e
cd "$(dirname "$0")"
CXX=${CXX:-c++}
OUT=${OUT:-./build/measure}
mkdir -p "$OUT/raw" "$OUT/pimpl"

echo "== 1. 单文件示例编译与运行"
for f in pimpl_basic fast_pimpl bench; do
  $CXX -std=c++17 -O2 -Wall -Wextra "$f.cpp" -o "$OUT/$f"
done

echo "-- pimpl_basic"
"$OUT/pimpl_basic"
echo "-- fast_pimpl"
"$OUT/fast_pimpl"

echo "== 2. 验证不完整类型析构编译错误（预期失败）"
if $CXX -std=c++17 -c pimpl_incomplete_ce.cpp -o "$OUT/ce.o" 2>"$OUT/ce.err"; then
  echo "ERROR: pimpl_incomplete_ce.cpp unexpectedly compiled!" >&2
  exit 1
else
  echo "OK: pimpl_incomplete_ce.cpp 按预期编译失败，首条错误："
  grep -m 3 "error:" "$OUT/ce.err" || head -n 5 "$OUT/ce.err"
fi

echo "== 3. 编译防火墙实测（5 个业务模块 + VideoPlayer.cpp + main.cpp）"
CALLERS="ui_bar playlist subtitle danmaku analytics main"

now_ms() {
  python3 -c 'import time; print(int(time.perf_counter() * 1000))'
}

# 3a. 全量编译耗时对比（5 轮取中位数）
measure_cmd() {
  label="$1"
  shift
  python3 - "$label" "$@" <<'PY'
import subprocess, sys, time
label = sys.argv[1]
cmd = sys.argv[2]
times = []
for _ in range(5):
    t0 = time.perf_counter()
    subprocess.run(cmd, shell=True, check=True)
    times.append((time.perf_counter() - t0) * 1000.0)
times.sort()
med = times[len(times) // 2]
print(f"  {label}: {med:.1f} ms (samples: {', '.join(f'{t:.0f}' for t in times)})")
PY
}

RAW_FULL_CMD="$CXX -std=c++17 -O2 -Icompile_test/raw -c compile_test/raw/VideoPlayer.cpp -o $OUT/raw/VideoPlayer.o"
for c in $CALLERS; do
  RAW_FULL_CMD="$RAW_FULL_CMD && $CXX -std=c++17 -O2 -Icompile_test/raw -c compile_test/callers/$c.cpp -o $OUT/raw/$c.o"
done

PIMPL_FULL_CMD="$CXX -std=c++17 -O2 -Icompile_test/pimpl -c compile_test/pimpl/VideoPlayer.cpp -o $OUT/pimpl/VideoPlayer.o"
for c in $CALLERS; do
  PIMPL_FULL_CMD="$PIMPL_FULL_CMD && $CXX -std=c++17 -O2 -Icompile_test/pimpl -c compile_test/callers/$c.cpp -o $OUT/pimpl/$c.o"
done

# 改一个私有成员（-DADD_PRIVATE_FIELD）：
# 普通头文件：VideoPlayer.h 变动，全部 7 个 .cpp（VideoPlayer.cpp + 5 业务模块 + main.cpp）都必须重编
RAW_INCR_CMD="$CXX -std=c++17 -O2 -DADD_PRIVATE_FIELD -Icompile_test/raw -c compile_test/raw/VideoPlayer.cpp -o $OUT/raw/VideoPlayer.o"
for c in $CALLERS; do
  RAW_INCR_CMD="$RAW_INCR_CMD && $CXX -std=c++17 -O2 -DADD_PRIVATE_FIELD -Icompile_test/raw -c compile_test/callers/$c.cpp -o $OUT/raw/$c.o"
done

# Pimpl 头文件：仅 VideoPlayer.cpp 变动，只需重编 1 个 .cpp！
PIMPL_INCR_CMD="$CXX -std=c++17 -O2 -DADD_PRIVATE_FIELD -Icompile_test/pimpl -c compile_test/pimpl/VideoPlayer.cpp -o $OUT/pimpl/VideoPlayer.o"

echo "-- 全量编译（7 个 .cpp，5 轮中位数）"
measure_cmd "普通头文件 (7/7 文件)" "$RAW_FULL_CMD"
measure_cmd "Pimpl 头文件 (7/7 文件)" "$PIMPL_FULL_CMD"

echo "-- 修改 1 个私有成员后的增量重编（5 轮中位数）"
measure_cmd "普通头文件 (重编 7 个 .cpp)" "$RAW_INCR_CMD"
measure_cmd "Pimpl 头文件 (仅重编 1 个 .cpp)" "$PIMPL_INCR_CMD"

# 链接并检查 sizeof(VideoPlayer) 变动（ABI 稳定性）
$CXX -std=c++17 -O2 -Icompile_test/raw -c compile_test/raw/VideoPlayer.cpp -o "$OUT/raw/VideoPlayer.o"
for c in $CALLERS; do
  $CXX -std=c++17 -O2 -Icompile_test/raw -c "compile_test/callers/$c.cpp" -o "$OUT/raw/$c.o"
done
$CXX "$OUT/raw/"*.o -o "$OUT/raw_before"
echo "-- ABI 检查"
printf "  raw   before: "; "$OUT/raw_before" | tail -n 1

$CXX -std=c++17 -O2 -DADD_PRIVATE_FIELD -Icompile_test/raw -c compile_test/raw/VideoPlayer.cpp -o "$OUT/raw/VideoPlayer.o"
for c in $CALLERS; do
  $CXX -std=c++17 -O2 -DADD_PRIVATE_FIELD -Icompile_test/raw -c "compile_test/callers/$c.cpp" -o "$OUT/raw/$c.o"
done
$CXX "$OUT/raw/"*.o -o "$OUT/raw_after"
printf "  raw   after : "; "$OUT/raw_after" | tail -n 1

$CXX "$OUT/pimpl/"*.o -o "$OUT/pimpl_after"
printf "  pimpl after : "; "$OUT/pimpl_after" | tail -n 1

echo "== 4. 运行期性能基准（bench）"
"$OUT/bench"
