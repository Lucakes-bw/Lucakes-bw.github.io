# lsm-kv：一个用来学习的 LSM 树键值存储

《数据库》专栏「存储引擎」一章的代码，C11。文章中的输出都来自这些程序：Docker 中的 Ubuntu 24.04 容器，内核 6.8，GCC 13.3；虚拟机 2 核、2 GB 内存。

```bash
make
./build/bench check /tmp/chk 1000000                      # 随机测试：和一个数组对照
./build/bench fill /tmp/db 2000000 100 rand 0 10          # 写入 200 万条，打印写放大和各层
./build/bench read /tmp/db 2000000 200000 no 10           # 随机查不存在的键，打印读放大
```

| 文件 | 内容 |
|---|---|
| `lsm.h`、`lsm.c` | 存储引擎：WAL、跳表内存表、SSTable、布隆过滤器、多路归并与分层合并、清单、恢复 |
| `bench.c` | `fill`：写入并统计写放大；`read`：随机读并统计读放大；`check`：正确性测试 |
| `crash.c` | 写入进程被 `SIGKILL` 后，检查已确认的写入是否都在；`nocheck` 保留现场 |
| `bloomtest.c` | 布隆过滤器的假阳性率与每键位数 |

简化之处（文章中有说明）：单线程；落盘和合并都是同步的；合并时整层重写，而不是像 RocksDB 那样每次只挑一个文件；没有块缓存、压缩和快照。
