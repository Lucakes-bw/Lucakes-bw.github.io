#!/usr/bin/env bash
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/../.." && pwd)"
CXX="${CXX:-clang++}"
OUT="$(mktemp -d /tmp/minilog_compile_bench.XXXXXX)"
trap 'rm -rf "$OUT"' EXIT

mkdir -p "$OUT/mono" "$OUT/pimpl"

CALLERS=(auth order payment inventory notify metrics main)

measure_cmd() {
  local label="$1"
  local cmd="$2"
  python3 - "$label" "$cmd" <<'PY'
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

# 1. 全量编译命令（8 个 .cpp：logger 实现 + 6 个业务模块 + main）
MONO_FULL="$CXX -std=c++17 -O2 -I$ROOT/legacy -c $ROOT/legacy/monolithic_logger.cpp -o $OUT/mono/monolithic_logger.o"
for c in "${CALLERS[@]}"; do
  MONO_FULL="$MONO_FULL && $CXX -std=c++17 -O2 -I$ROOT/legacy -c $DIR/$c.cpp -o $OUT/mono/$c.o"
done

PIMPL_CORE="$CXX -std=c++17 -O2 -I$ROOT/include -c $ROOT/src/pattern.cpp -o $OUT/pimpl/pattern.o"
PIMPL_CORE="$PIMPL_CORE && $CXX -std=c++17 -O2 -I$ROOT/include -c $ROOT/src/filter.cpp -o $OUT/pimpl/filter.o"
PIMPL_CORE="$PIMPL_CORE && $CXX -std=c++17 -O2 -I$ROOT/include -c $ROOT/src/sinks.cpp -o $OUT/pimpl/sinks.o"
PIMPL_CORE="$PIMPL_CORE && $CXX -std=c++17 -O2 -I$ROOT/include -c $ROOT/src/async_worker.cpp -o $OUT/pimpl/async_worker.o"
PIMPL_CORE="$PIMPL_CORE && $CXX -std=c++17 -O2 -I$ROOT/include -c $ROOT/src/logger.cpp -o $OUT/pimpl/logger.o"
PIMPL_FULL="$PIMPL_CORE"
for c in "${CALLERS[@]}"; do
  PIMPL_FULL="$PIMPL_FULL && $CXX -std=c++17 -O2 -DUSE_PIMPL -I$ROOT/include -c $DIR/$c.cpp -o $OUT/pimpl/$c.o"
done

# 2. 模拟在私有成员区增加 1 个 int retryCount_ 字段后的增量重编
# 普通头文件：monolithic_logger.h 变动，全部 8 个 .cpp 必须重编
MONO_INCR="$CXX -std=c++17 -O2 -DADD_PRIVATE_FIELD -I$ROOT/legacy -c $ROOT/legacy/monolithic_logger.cpp -o $OUT/mono/monolithic_logger.o"
for c in "${CALLERS[@]}"; do
  MONO_INCR="$MONO_INCR && $CXX -std=c++17 -O2 -DADD_PRIVATE_FIELD -I$ROOT/legacy -c $DIR/$c.cpp -o $OUT/mono/$c.o"
done

# Pimpl 头文件：仅 src/logger.cpp 需要重编，6 个业务模块 + main.cpp 全部 0 改动
PIMPL_INCR="$CXX -std=c++17 -O2 -DMINILOG_ADD_PRIVATE_FIELD -I$ROOT/include -c $ROOT/src/logger.cpp -o $OUT/pimpl/logger.o"

echo "-- 全量编译（5 轮中位数）"
measure_cmd "普通头文件（8/8 个 .cpp）" "$MONO_FULL"
measure_cmd "Pimpl 头文件（12/12 个 .cpp，含完整 minilog 实现）" "$PIMPL_FULL"

echo "-- 私有区新增 1 个 int 字段后的增量重编（5 轮中位数）"
measure_cmd "普通头文件（重编 8/8 个 .cpp）" "$MONO_INCR"
measure_cmd "Pimpl 头文件（仅重编 1 个 src/logger.cpp）" "$PIMPL_INCR"

echo "-- ABI 对象尺寸检查"
$CXX -std=c++17 -O2 -I$ROOT/legacy -c "$ROOT/legacy/monolithic_logger.cpp" -o "$OUT/mono/monolithic_logger.o"
for c in "${CALLERS[@]}"; do
  $CXX -std=c++17 -O2 -I$ROOT/legacy -c "$DIR/$c.cpp" -o "$OUT/mono/$c.o"
done
$CXX "$OUT/mono/"*.o -o "$OUT/mono_before"
printf "  MonoLogger before: "; "$OUT/mono_before" | tail -n 1

$CXX -std=c++17 -O2 -DADD_PRIVATE_FIELD -I$ROOT/legacy -c "$ROOT/legacy/monolithic_logger.cpp" -o "$OUT/mono/monolithic_logger.o"
for c in "${CALLERS[@]}"; do
  $CXX -std=c++17 -O2 -DADD_PRIVATE_FIELD -I$ROOT/legacy -c "$DIR/$c.cpp" -o "$OUT/mono/$c.o"
done
$CXX "$OUT/mono/"*.o -o "$OUT/mono_after"
printf "  MonoLogger after : "; "$OUT/mono_after" | tail -n 1

$CXX -std=c++17 -O2 -DMINILOG_ADD_PRIVATE_FIELD -I$ROOT/include -c "$ROOT/src/logger.cpp" -o "$OUT/pimpl/logger.o"
$CXX "$OUT/pimpl/"*.o -lpthread -o "$OUT/pimpl_after"
printf "  minilog    after : "; "$OUT/pimpl_after" | tail -n 1
