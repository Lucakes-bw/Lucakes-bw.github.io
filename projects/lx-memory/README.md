# Linux 内存管理：实验程序

《Linux 基础》专栏「内存管理」一章的实验程序，C11。文章中的输出都来自这些程序：Ubuntu 24.04 容器（aarch64），内核 6.8，GCC 13.3，glibc 2.39；虚拟机 2 核、2 GB 内存，没有交换分区。

```bash
make
./build/vm
```

部分实验需要 root 和特权容器（清空页缓存、修改 `vm.overcommit_memory`、读 `dmesg`），文章中有说明。

| 程序 | 文章 | 内容 |
|---|---|---|
| `vm` | 虚拟内存与缺页 | 地址空间的各个区域；VSZ 和 RSS；缺页、零页、透明大页 |
| `malloc where\|frag\|overhead\|threads N` | malloc 是怎样分配内存的 | brk 和 mmap 的分界；碎片与 malloc_trim；块的开销；多线程的 arena |
| `pagecache write\|stream\|read\|direct\|cached\|evict 文件 [MB]` | 页缓存与写回 | write 与 fsync；脏页限速；冷读热读；O_DIRECT；mincore；posix_fadvise |
| `cow` | fork 与写时复制 | fork 的耗时；写时复制；透明大页下的写时复制；posix_spawn |
| `oom commit\|eat 步长MB\|hold MB` | 内存不足：overcommit 与 OOM killer | 三种 overcommit 模式；在受限的容器中触发 OOM |

`oom eat` 会一直占用内存直到被杀，只在设置了内存上限的容器中运行。
