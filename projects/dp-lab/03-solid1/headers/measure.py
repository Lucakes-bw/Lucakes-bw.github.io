"""《SOLID 原则（上）》：比较 use_all.cpp 和 use_stats.cpp 的预处理行数和编译时间（各编译 9 次取中位数）"""
import os, statistics, subprocess, sys, time
cxx = sys.argv[1] if len(sys.argv) > 1 else 'clang++'
here = os.path.dirname(os.path.abspath(__file__))
for src in ['use_all.cpp', 'use_stats.cpp']:
    path = os.path.join(here, src)
    pp = subprocess.run([cxx, '-std=c++17', '-E', path], capture_output=True, text=True, check=True).stdout
    times = []
    for _ in range(9):
        t0 = time.perf_counter()
        subprocess.run([cxx, '-std=c++17', '-O0', '-c', path, '-o', os.devnull], check=True)
        times.append((time.perf_counter() - t0) * 1000)
    print(f'{src:14} 预处理后 {len(pp.splitlines()):6} 行   编译 {statistics.median(times):6.0f} ms（中位数）')
