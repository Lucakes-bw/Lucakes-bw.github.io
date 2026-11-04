# 数据结构补充：示例程序

《数据结构》专栏中四篇文章的示例程序，C11。文章中的输出和时间都来自这些程序（Apple M6，Apple Clang 21，`-O2`）。

| 文件 | 文章 |
|---|---|
| `rbtree.c` | 红黑树：插入、删除、五条性质的检查，和 AVL 树比较 |
| `skiplist.c` | 跳表：查找、插入、删除，层数的分布 |
| `trie.c` | Trie：前缀计数、按前缀列出单词，用系统词典测内存和速度（需要 `/usr/share/dict/words`） |
| `lru.c` | LRU 缓存：哈希表 + 双向链表，LRU、FIFO、随机淘汰的命中率 |

```bash
make run        # 运行全部
make asan       # 用 AddressSanitizer 检查
```

每个程序都和一个暴力实现（或布尔数组、排序数组）对比结果，不一致会立即退出。
