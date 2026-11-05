---
title: strace 与 ltrace：代价与用法
date: 2026-11-10 10:10:00
updated: 2026-11-10 10:10:00
categories:
  - Linux基础
  - 性能分析
tags:
  - Linux
  - 性能
  - strace
order: 2
description: strace 能看到程序的每一个系统调用，代价是程序被拖慢：实测一次 getppid 从 104 纳秒变成 25.9 微秒，慢了 200 多倍；只跟踪 openat 也一样慢，加上 --seccomp-bpf 才恢复到 145 纳秒。ltrace 跟踪 malloc 更夸张，一次 malloc + free 从 4 纳秒变成 98.7 微秒。本文讲清楚这些开销从哪里来，再用 strace 找出一个程序启动慢 2.3 秒的原因：strace -c 默认统计的是 CPU 时间，fsync 只显示 12 毫秒，加 -w 才看到它等了 1.3 秒；最后看一个卡住不动的进程在等什么。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解 strace 和 ltrace 的原理（ptrace、断点），知道它们让程序变慢多少、为什么；会用 `--seccomp-bpf` 减小开销，知道 `perf trace` 和 `perf stat` 这些开销更小的替代；会用 `-c -w`、`-T`、`-tt` 找出耗时长的系统调用；会用 `strace -p`、`/proc/<pid>/wchan`、`/proc/<pid>/stack` 查看卡住的进程。
>
> **前置知识**：本专栏的《文件描述符与系统调用》（strace 的基本用法：`-e trace=`、`-c`、`-p`）；本章第 1 篇。
>
> **实验环境**：同第 1 篇；strace 6.8，ltrace 0.7.3。

## 1. strace 是怎样工作的

《文件描述符与系统调用》中已经用 strace 看过程序执行了哪些系统调用。它的原理是 Linux 的 **ptrace** 系统调用，也就是 GDB 用的那一套：

1. strace 用 `PTRACE_SEIZE` 附着到目标进程，并要求在每次系统调用的**进入**和**返回**时停下来；
2. 目标进程每执行一个系统调用，内核就让它暂停，通知 strace；
3. strace 被唤醒，用 `PTRACE_GETREGSET` 读出寄存器（系统调用号、参数、返回值），需要时再用 `process_vm_readv` 读出参数指向的字符串，格式化后输出；
4. strace 让目标进程继续运行。

<figure class="diagram">
<svg viewBox="0 0 700 290" role="img" aria-label="strace 的原理：目标进程每执行一个系统调用，在进入和返回时各停一次，内核唤醒 strace，strace 读出寄存器和参数、输出一行，再让目标进程继续。原本一次进入内核，变成了四次进程切换">
  <defs><marker id="ptr-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="ptr-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="20" y="14" width="140" height="32" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="90.0" y="34.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">目标程序</text><line x1="90" y1="46" x2="90" y2="280" style="stroke:var(--border-2);stroke-width:1.2;stroke-dasharray:4 4"/><rect x="280" y="14" width="140" height="32" rx="6" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="350.0" y="34.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">内核</text><line x1="350" y1="46" x2="350" y2="280" style="stroke:var(--border-2);stroke-width:1.2;stroke-dasharray:4 4"/><rect x="540" y="14" width="140" height="32" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="610.0" y="34.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">strace</text><line x1="610" y1="46" x2="610" y2="280" style="stroke:var(--border-2);stroke-width:1.2;stroke-dasharray:4 4"/><line x1="96" y1="70" x2="344" y2="70" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#ptr-a)"/><text x="220.0" y="63.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">getppid()：进入内核</text><line x1="356" y1="100" x2="604" y2="100" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#ptr-a)"/><text x="480.0" y="93.0" text-anchor="middle" style="fill:var(--link);font-size:11px">暂停目标，唤醒 strace</text><line x1="604" y1="130" x2="356" y2="130" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#ptr-a)"/><text x="480.0" y="123.0" text-anchor="middle" style="fill:var(--link);font-size:11px">读寄存器：调用号、参数</text><line x1="356" y1="175" x2="604" y2="175" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#ptr-a)"/><text x="480.0" y="168.0" text-anchor="middle" style="fill:var(--link);font-size:11px">返回前再暂停，唤醒 strace</text><line x1="604" y1="205" x2="356" y2="205" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#ptr-a)"/><text x="480.0" y="198.0" text-anchor="middle" style="fill:var(--link);font-size:11px">读返回值，输出一行，继续</text><line x1="344" y1="235" x2="96" y2="235" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#ptr-a)"/><text x="220.0" y="228.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">回到程序</text><rect x="196" y="142" width="148" height="22" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="270.0" y="157.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">内核执行 getppid</text><text x="350.0" y="268.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">每个系统调用多出 4 次进程切换：实测 getppid 从 104 ns 变成约 25 µs</text>
  </g>
</svg>
</figure>

一个系统调用本来只需要进入一次内核；被 strace 跟踪时，要多出两次「暂停目标进程 → 切换到 strace → strace 做完再切换回来」。每次切换都是一次进程调度，代价是微秒级。

## 2. 被跟踪的程序慢了多少

`syscost getppid N` 连续执行 N 次 `getppid`（最简单的系统调用之一，只返回父进程的进程号），打印平均每次的耗时：

```c
double t0 = nowSeconds();
for (long i = 0; i < n; i++) syscall(SYS_getppid);
double t = nowSeconds() - t0;
printf("getppid %ld 次：%.3f 秒，每次 %.0f ns\n", n, t, t / n * 1e9);
```

100 万次，分别在不同的工具下运行（直接运行测了 3 次：130、104、104 纳秒）：

| 运行方式 | 每次 getppid | 慢了多少 |
|---|---|---|
| 直接运行 | 104 ns | — |
| `perf stat -e raw_syscalls:sys_enter` | 117 ns | 几乎没有 |
| `strace --seccomp-bpf -f -e trace=openat` | 145 ns | 几乎没有 |
| `bpftrace` 按系统调用号计数 | 163 ns | +60 ns |
| `perf trace -s` | 4951 ns | 48 倍 |
| `strace -e trace=openat` | 23575 ns | 227 倍 |
| `strace -c` | 23981 ns | 231 倍 |
| `strace -o 文件` | 25940 ns | 249 倍 |

<figure class="diagram">
<svg viewBox="0 0 700 300" role="img" aria-label="100 万次 getppid 在不同工具下每次的耗时（对数坐标）。perf stat 计数、strace --seccomp-bpf 和 bpftrace 计数几乎没有开销；perf trace 慢 48 倍；strace 慢 200 多倍，只跟踪 openat 也一样慢">
  <defs><marker id="cst-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="cst-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="190.0" y="34.0" text-anchor="end" style="fill:var(--fg);font-size:12px">直接运行</text><rect x="200.0" y="20.0" width="47.7" height="18.0" rx="2" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="255.7" y="34.0" text-anchor="start" style="fill:var(--muted);font-size:11px">104 ns</text><text x="190.0" y="64.0" text-anchor="end" style="fill:var(--fg);font-size:12px">perf stat 计数</text><rect x="200.0" y="50.0" width="55.4" height="18.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="263.4" y="64.0" text-anchor="start" style="fill:var(--muted);font-size:11px">117 ns</text><text x="190.0" y="94.0" text-anchor="end" style="fill:var(--fg);font-size:12px">strace --seccomp-bpf</text><rect x="200.0" y="80.0" width="69.4" height="18.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="277.4" y="94.0" text-anchor="start" style="fill:var(--muted);font-size:11px">145 ns</text><text x="190.0" y="124.0" text-anchor="end" style="fill:var(--fg);font-size:12px">bpftrace 计数</text><rect x="200.0" y="110.0" width="77.0" height="18.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="285.0" y="124.0" text-anchor="start" style="fill:var(--muted);font-size:11px">163 ns</text><text x="190.0" y="154.0" text-anchor="end" style="fill:var(--fg);font-size:12px">perf trace -s</text><rect x="200.0" y="140.0" width="299.4" height="18.0" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="507.4" y="154.0" text-anchor="start" style="fill:var(--muted);font-size:11px">4951 ns</text><text x="190.0" y="184.0" text-anchor="end" style="fill:var(--fg);font-size:12px">strace -e trace=openat</text><rect x="200.0" y="170.0" width="401.0" height="18.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="609.0" y="184.0" text-anchor="start" style="fill:var(--muted);font-size:11px">23575 ns</text><text x="190.0" y="214.0" text-anchor="end" style="fill:var(--fg);font-size:12px">strace -c</text><rect x="200.0" y="200.0" width="402.1" height="18.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="610.1" y="214.0" text-anchor="start" style="fill:var(--muted);font-size:11px">23981 ns</text><text x="190.0" y="244.0" text-anchor="end" style="fill:var(--fg);font-size:12px">strace -o 文件</text><rect x="200.0" y="230.0" width="407.2" height="18.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="615.2" y="244.0" text-anchor="start" style="fill:var(--muted);font-size:11px">25940 ns</text><line x1="245.2" y1="262" x2="245.2" y2="268" style="stroke:var(--border-2)"/><text x="245.2" y="284.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">100 ns</text><line x1="395.2" y1="262" x2="395.2" y2="268" style="stroke:var(--border-2)"/><text x="395.2" y="284.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">1 µs</text><line x1="545.2" y1="262" x2="545.2" y2="268" style="stroke:var(--border-2)"/><text x="545.2" y="284.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">10 µs</text><line x1="200" y1="262" x2="635.5" y2="262" style="stroke:var(--border-2)"/>
  </g>
</svg>
</figure>

几个结论：

- **strace 让每个系统调用慢了 200 多倍**。一个每秒处理几万个请求的服务，被 strace 附着后可能只剩几百个，甚至触发超时。在生产环境对重要的进程用 `strace -p` 之前要想清楚。
- **`-e trace=openat` 只是不显示，并不能减小开销**。目标进程仍然在每个系统调用时停下来，strace 看一眼发现不是 `openat`，再让它继续。100 万次 `getppid` 一行都没有输出，却和全部输出一样慢。
- **`--seccomp-bpf` 才真正过滤**。strace 给目标进程装一个 seccomp 过滤器，只有 `openat` 才会停下来，其他系统调用直接执行，开销几乎为零。它要求同时加 `-f`（跟踪子进程），因为过滤器会被子进程继承。
- **只统计次数时，用 `perf stat` 或 bpftrace**。它们利用内核中的跟踪点（tracepoint）计数，不需要暂停进程。`perf trace` 也用跟踪点，但要把每个事件的参数传到用户态再处理，开销介于两者之间。

## 3. ltrace：库函数的调用

strace 只能看到系统调用。`malloc`、`strlen`、`printf` 这些是 C 库中的函数，不经过内核。**ltrace** 跟踪的是程序对共享库函数的调用：它在每个库函数的 PLT 项（程序调用共享库函数的跳板，见《静态库与动态库》）上设置**断点**，程序每调用一次库函数，就触发一次断点异常，暂停下来让 ltrace 处理。

这比 strace 还要重。`syscost malloc N` 连续 `malloc(32)` + `free`：

```c
for (long i = 0; i < n; i++) {
    void* volatile p = malloc(32);
    free(p);
}
```

| 运行方式 | 每次 malloc + free |
|---|---|
| 直接运行 | 4 ns |
| `ltrace -c` | 98682 ns |

慢了两万多倍：`malloc` 和 `free` 本身只要几纳秒（tcache 中直接取，见《malloc 是怎样分配内存的》），断点的处理却要几十微秒。`ltrace -c` 的统计：

```
% time     seconds  usecs/call     calls      function
------ ----------- ----------- --------- --------------------
 50.01   33.144603          33   1000000 malloc
 49.99   33.126803          33   1000000 free
  0.00    0.000075          75         1 __printf_chk
```

次数是准确的，`usecs/call` 却是被 ltrace 放大以后的时间，不能当真。ltrace 适合回答「调用了哪些库函数、各多少次」，比如统计 `ls -l /usr/bin` 调用最多的库函数：

```
$ ltrace -c ls /usr/bin > /dev/null
% time     seconds  usecs/call     calls      function
------ ----------- ----------- --------- --------------------
 36.56    0.225471          33      6782 __errno_location
 32.58    0.200953          33      6046 strcoll
  8.16    0.050316          34      1468 strlen
```

`strcoll` 6046 次是排序文件名时按语言环境比较字符串。要知道库函数的耗时，用 bpftrace 的 uprobe（第 6 篇），开销小得多。

## 4. 找出启动慢的原因

`syscost startup` 模拟一个启动很慢的程序：读 20 次配置文件，写 128 MB 日志并 `fsync`，向一个不回应的服务发请求、最多等 1 秒。

```bash
$ time ./build/syscost startup /data
启动完成，用了 2.34 秒

real	0m2.336s
user	0m0.000s
sys	0m0.041s
```

按第 1 篇的方法：user + sys 只有 0.04 秒，2.3 秒几乎全在等。先用 `strace -c` 统计：

```
$ strace -c ./build/syscost startup /data
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 63.58    0.043740         336       130           write
 17.33    0.011921       11921         1           fsync
 16.83    0.011579         503        23           openat
  1.42    0.000974           9       101           read
  0.52    0.000356          14        25           close
  0.11    0.000076          76         1           ppoll
```

加起来只有 0.07 秒，排第一的是 `write`。这张表把人带偏了：**`strace -c` 默认统计的是系统调用在内核中消耗的 CPU 时间**，等待的时间不算。`fsync` 和 `ppoll` 绝大部分时间在睡眠，几乎不消耗 CPU。加上 `-w`，改为统计墙上时钟的时间：

```
$ strace -c -w ./build/syscost startup /data
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 55.55    1.298656     1298656         1           fsync
 43.04    1.006351     1006351         1           ppoll
  0.94    0.021961         168       130           write
  0.36    0.008459         367        23           openat
```

真相出来了：`fsync` 1.3 秒（128 MB 写到限速 100 MB/s 的磁盘），`ppoll` 1.0 秒（等那个不回应的服务直到超时）。再用 `-T`（每个调用的耗时）和 `-tt`（精确到微秒的时刻）看具体的调用：

```
$ strace -T -tt -e trace=fsync,ppoll,socketpair ./build/syscost startup /data
02:44:16.472372 fsync(3)                = 0 <1.300210>
02:44:17.772920 socketpair(AF_UNIX, SOCK_STREAM, 0, [3, 4]) = 0 <0.000195>
02:44:17.773318 ppoll([{fd=3, events=POLLIN}], 1, {tv_sec=1, tv_nsec=0}, NULL, 0) = 0 (Timeout) <1.003969>
```

`(Timeout)` 说明等了整整 1 秒也没有回应。

这里还有一个小坑：代码中调用的是 `poll`，但 `-e trace=poll` 什么也抓不到。aarch64 的 Linux 没有 `poll` 系统调用，glibc 用 `ppoll` 实现了它。不确定名字时，先不加 `-e` 看一遍。

另外，strace 让每个系统调用变慢，这次的 2.37 秒比不跟踪时的 2.34 秒长了一点；对于系统调用很多的程序，strace 测出的时间可能比实际长得多。用它找「哪一个调用慢」很合适，用它测「总共多慢」要小心。

## 5. 卡住的进程

一个进程不动了，没有输出，CPU 是 0。`syscost stuck` 模拟这种情况：从一个命名管道读数据，没有人写，就一直等着。先不用 strace，看 `/proc`：

```
$ ps -o pid,stat,wchan:20,comm -p 1121
    PID STAT WCHAN                COMMAND
   1121 S    pipe_read            syscost
$ cat /proc/1121/stack
[<0>] pipe_read+0x40c/0x480
[<0>] vfs_read+0x318/0x348
[<0>] ksys_read+0x118/0x140
[<0>] __arm64_sys_read+0x28/0x50
[<0>] invoke_syscall+0x7c/0x130
...
```

- `STAT` 是 `S`（可中断的睡眠），不是 `D`，说明不是在等磁盘；
- `WCHAN`（wait channel）是进程在内核中睡眠的函数：`pipe_read`，在读管道；
- `/proc/<pid>/stack` 是完整的内核调用栈（需要 root）：从 `read` 系统调用进入，在 `pipe_read` 中等待。

这几步完全不影响进程。需要更多信息时再用 `strace -p` 附着：

```
$ timeout 2 strace -p 1121
strace: Process 1121 attached
read(3, strace: Process 1121 detached
 <detached ...>
```

`read(3, ` 没有结束，说明进程正卡在对 3 号描述符的 `read` 中。`ls -l /proc/1121/fd/3` 可以看到它是哪个文件。

strace 有一个 `-k` 选项，在每个系统调用旁边打印用户态的调用栈，可以看出是哪段代码发起了这个调用。但 Ubuntu 24.04 的 strace 编译时没有启用它（`Stack traces (-k/--stack-trace option) are not supported by this build of strace`）。可以用 GDB 附着后 `bt`，或者用第 6 篇的 bpftrace。

## 6. 常见错误

1. **在生产环境随手 `strace -p` 一个繁忙的进程**。每个系统调用慢 200 多倍，服务可能因此超时。先用 `/proc/<pid>/wchan`、`stack`，或者开销小的 `perf stat`、bpftrace。
2. **以为 `-e trace=` 能减小开销**。只有 `--seccomp-bpf` 才能。
3. **用 `strace -c` 找等待时间**。默认统计的是 CPU 时间，等待长的调用（`fsync`、`poll`、`read` 网络）会显得很短，要加 `-w`。
4. **相信 `ltrace` 显示的耗时**。断点让每次调用多出几十微秒，只有次数可信。
5. **按调用的函数名过滤系统调用**。`poll` 在 aarch64 上是 `ppoll`，`open` 是 `openat`，`stat` 可能是 `newfstatat` 或 `statx`。

## 7. 小结

- strace 用 ptrace 让目标进程在每个系统调用的进入和返回时停下来，ltrace 在库函数的 PLT 上设置断点；
- 实测：`getppid` 从 104 纳秒变成约 25 微秒；`-e trace=openat` 一样慢，`--seccomp-bpf` 恢复到 145 纳秒；`malloc` + `free` 在 ltrace 下从 4 纳秒变成 98.7 微秒；
- 只统计次数，用 `perf stat -e` 或 bpftrace，几乎没有开销；
- `strace -c` 默认统计 CPU 时间，找等待要加 `-w`；`-T`、`-tt` 显示每个调用的耗时和时刻；
- 卡住的进程，先看 `ps` 的 `STAT`、`WCHAN` 和 `/proc/<pid>/stack`，再 `strace -p`。

## 8. 练习

1. 一个 Web 服务每秒处理 2 万个请求，每个请求大约 10 个系统调用。用 `strace -p` 附着以后，按本文的数据估算，它每秒还能处理多少个请求？
2. `strace -c -f make` 显示 `wait4` 只占 0.1% 的时间，`strace -c -w -f make` 却显示它占 90%。为什么差这么多？哪一个结果说明 `make` 的时间主要花在等编译器上？
3. 一个进程的 `STAT` 是 `D`，`WCHAN` 是 `nfs_wait_bit_killable`。它在等什么？`kill -9` 能立刻结束它吗？

<details>
<summary>点击查看答案</summary>

1. 每个请求 10 个系统调用，被跟踪后每个多出约 25 微秒，也就是每个请求多 250 微秒。只考虑这部分开销，一个线程每秒最多处理 1 / 0.00025 = 4000 个请求；而且 strace 是单线程的，所有线程的系统调用都要排队等它处理，实际可能更少。从 2 万降到几千甚至更低。

2. `wait4` 是 `make` 等待子进程（编译器）结束，几乎全是睡眠，不消耗 CPU。不加 `-w` 时按 CPU 时间统计，它很短；加 `-w` 按墙上时钟统计，等待的时间都算进去，占了 90%。后者说明 `make` 自己几乎不干活，时间主要花在等编译器上，要加快构建应该看编译器（比如并行编译 `make -j`），而不是 `make` 本身。

3. `D` 是不可中断的睡眠，`nfs_wait_bit_killable` 说明它在等 NFS（网络文件系统）的服务器回应，可能是网络断了或者服务器卡住了。函数名中的 `killable` 表示这种等待可以被致命信号打断，所以 `kill -9` 通常能结束它；而在普通的 `D` 状态（比如等本地磁盘的某些路径）中，连 `kill -9` 也要等 IO 完成后才生效。它还会让平均负载升高（第 1 篇）。

</details>
