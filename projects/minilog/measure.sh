#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

echo "=== 1. Release (-O2) 构建与 CTest 单测 ==="
cmake -B build-rel -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build-rel -j4 >/dev/null
ctest --test-dir build-rel --output-on-failure

echo ""
echo "=== 2. ASan + UBSan 检查（全部单测）==="
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -O1" >/dev/null
cmake --build build-asan -j4 >/dev/null
ctest --test-dir build-asan --output-on-failure

echo ""
echo "=== 3. TSan 并发竞争检查（test_async）==="
if cmake -B build-tsan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer -O1" >/dev/null 2>&1 \
  && cmake --build build-tsan --target test_async -j4 >/dev/null 2>&1; then
  if ./build-tsan/test_async; then
    echo "TSan test_async passed cleanly"
  else
    echo "TSan failed with exit code $?" >&2
    exit 1
  fi
else
  echo "TSan build unavailable on this toolchain"
fi

echo ""
echo "=== 4. 6.1 编译防火墙实测 ==="
bash legacy/compile_bench/run_compile_bench.sh

echo ""
echo "=== 5. 6.2 Sink 分派与 SBO 堆分配实测 ==="
./build-rel/bench_sink_dispatch

echo ""
echo "=== 6. 6.3 异步队列吞吐、P50/P99 延迟与 CRTP 实测 ==="
./build-rel/bench_async_throughput

echo ""
echo "=== 7. 6.4 三版本结构指标、编译耗时与运行耗时 ==="
python3 - "$ROOT" <<'PY'
import os, re, subprocess, tempfile, time

root = os.sys.argv[1]
def count_metrics(files):
    loc = 0
    classes = 0
    for f in files:
        with open(os.path.join(root, f), 'r', encoding='utf-8') as fp:
            text = fp.read()
        lines = [ln for ln in text.splitlines() if ln.strip() and not ln.strip().startswith('//')]
        loc += len(lines)
        classes += len(re.findall(r'\b(?:class|struct)\s+\w+\s*(?:final\s*)?(?::\s*public|\{)', text))
    return loc, classes

mono_files = ['legacy/monolithic_logger.h', 'legacy/monolithic_logger.cpp']
over_files = ['legacy/overengineered_logger.cpp']
mini_files = [
    'include/minilog/level.h', 'include/minilog/record.h', 'include/minilog/async.h',
    'include/minilog/pattern.h', 'include/minilog/filter.h', 'include/minilog/sink.h',
    'include/minilog/logger.h', 'src/pattern.cpp', 'src/filter.cpp',
    'src/sinks.cpp', 'src/async_worker.h', 'src/async_worker.cpp', 'src/logger.cpp'
]

for name, flist in [('Monolithic', mono_files), ('Over-engineered GoF', over_files), ('minilog (full lib)', mini_files)]:
    loc, cls = count_metrics(flist)
    print(f"  {name:22s}: non-empty LOC={loc:4d}, class/struct defs={cls}")

with tempfile.TemporaryDirectory() as td:
    def compile_med(label, cmds):
        ts = []
        for _ in range(5):
            t0 = time.perf_counter()
            for c in cmds:
                subprocess.run(c, shell=True, check=True)
            ts.append((time.perf_counter() - t0) * 1000.0)
        ts.sort()
        print(f"  compile {label:20s}: {ts[2]:.1f} ms")

    compile_med("Monolithic", [
        f"clang++ -std=c++17 -O2 -c {root}/legacy/monolithic_logger.cpp -o {td}/mono.o"
    ])
    compile_med("Over-engineered GoF", [
        f"clang++ -std=c++17 -O2 -c {root}/legacy/overengineered_logger.cpp -o {td}/over.o"
    ])
    compile_med("minilog (sync core)", [
        f"clang++ -std=c++17 -O2 -I{root}/include -c {root}/src/pattern.cpp -o {td}/pat.o",
        f"clang++ -std=c++17 -O2 -I{root}/include -c {root}/src/filter.cpp -o {td}/flt.o",
        f"clang++ -std=c++17 -O2 -I{root}/include -c {root}/src/sinks.cpp -o {td}/snk.o",
        f"clang++ -std=c++17 -O2 -I{root}/include -c {root}/src/async_worker.cpp -o {td}/asw.o",
        f"clang++ -std=c++17 -O2 -I{root}/include -c {root}/src/logger.cpp -o {td}/log.o",
    ])
PY
./build-rel/bench_three_ways
