# Linux 性能分析：实验程序

《Linux 基础》专栏「性能分析」一章的实验程序，C11。文章中的输出都来自这些程序：Ubuntu 24.04 容器（aarch64），内核 6.8，GCC 13.3，perf 6.8，bpftrace 0.20.2，valgrind 3.22；虚拟机 2 核、2 GB 内存，数据盘限速 100 MB/s。

```bash
make
./build/load spin 2
```

perf、bpftrace 需要 root；在容器中要 `--privileged --pid=host`，并挂载 tracefs（`mount -t tracefs nodev /sys/kernel/tracing`）。

| 程序 | 文章 | 内容 |
|---|---|---|
| `load spin\|tiny\|sleep\|threads\|dread` | 性能分析从哪里入手 | 几种「慢」：计算、系统调用、睡眠、多线程、等磁盘 |
| `syscost getppid\|malloc\|startup\|stuck` | strace 与 ltrace | 跟踪的开销；找出慢的系统调用；卡住的进程 |
| `sample`、`sample-omit`、`sample-nosib` | perf 采样的原理与陷阱 | 同一个程序的三种编译方式：尾调用、省略帧指针 |
| `cache rowcol\|latency\|branch\|share`、`cache-branchy` | CPU 缓存与分支预测 | 按行与按列；内存延迟；分支预测；伪共享 |
| `offcpu 文件 线程数 请求数 [f][n][l]` | off-CPU 分析 | 解析、持锁查表、读磁盘的模拟服务 |
| `trace serve\|empty` | bpftrace | 被跟踪的服务；uprobe 的开销 |

`scripts/` 中：

- `loadexp.sh`：第 1 篇的平均负载实验，约 10 分钟；
- `offcpu.bt`：第 5 篇的 off-CPU 统计，`bpftrace scripts/offcpu.bt offcpu 5`；
- `fold.py`：把 `offcpu.bt` 的输出折叠成 [FlameGraph](https://github.com/brendangregg/FlameGraph) 的格式，`python3 scripts/fold.py off.txt > off.folded; flamegraph.pl off.folded > off.svg`。
