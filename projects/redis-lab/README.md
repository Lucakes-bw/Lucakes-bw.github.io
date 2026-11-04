# Redis 实验程序

《数据库》专栏「Redis」一章的实验程序，C11 + hiredis。文章中的输出都来自这些程序：Redis 8.10.3（Docker 镜像 `redis:8`）、MySQL 8.4.13，程序在 Ubuntu 24.04 容器中编译运行（GCC 13.3，hiredis 1.2.0）。

```bash
apt install libhiredis-dev libmysqlclient-dev
make
REDIS_HOST=172.17.0.4 MYSQL_HOST=172.17.0.5 build/vsmysql
```

服务器地址从环境变量 `REDIS_HOST`、`REDIS_PORT`、`MYSQL_HOST` 读取（默认 127.0.0.1）。MySQL 使用 root / lab 账号，程序会创建数据库 `rlab`。**这些程序会执行 `FLUSHALL`、修改配置，只能在实验用的实例上运行。**

| 程序 | 文章 | 内容 |
|---|---|---|
| `atomic` | Redis 入门 | 8 个线程各加 1 万次：INCR 与「先 GET 再 SET」 |
| `vsmysql` | Redis 入门 | 逐个按主键读 10 万次：Redis 与 MySQL |
| `pipeline [个数]` | 单线程、Pipeline 与慢命令 | 不同批量大小的 Pipeline、MGET |
| `block` | 单线程、Pipeline 与慢命令 | KEYS、SCAN、DEL、UNLINK 期间其他客户端的延迟 |
| `memory` | 底层编码与内存 | 三种存法的内存；有序集合的编码转换；listpack 中 HGET 的耗时 |
| `expire` | 过期与内存淘汰 | 定期删除和惰性删除 |
| `evict` | 过期与内存淘汰 | 各种淘汰策略的命中率，和精确 LRU 比较 |
| `persist bench\|write\|read\|fork` | 持久化 | 吞吐量；kill -9 前后的计数器；fork 和写时复制 |
| `repl write [wait]\|read\|failover 秒数` | 主从复制与哨兵 | 异步复制丢失的写入、WAIT；通过哨兵自动切换 |
| `cache aside\|stampede\|penetration\|avalanche\|race` | 用 Redis 给 MySQL 做缓存 | 旁路缓存；击穿、穿透、雪崩；一致性 |

`persist`、`repl` 的实验需要配合 `docker kill`、`docker pause` 等操作，步骤见对应的文章。
