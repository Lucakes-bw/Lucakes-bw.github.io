# dp-lab：设计模式专栏的配套代码

每篇文章一个目录（`02-uml/` 对应「基础与设计原则」的第 2 篇《UML 类图》），目录下每个 `.cpp` 是一个独立的程序。

```bash
cmake -B build && cmake --build build
./build/02-uml/lifetime
```

也可以单独编译某一个文件：

```bash
g++ -std=c++17 -Wall -Wextra -o lifetime 02-uml/lifetime.cpp
```

| 目录 | 文章 | 程序 |
| --- | --- | --- |
| `02-uml` | UML 类图 | `lifetime`（组合、聚合、关联的生命周期）、`lifetime_owned`（练习 4）、`player`（从代码画类图） |

文中的输出来自 macOS 上的 Apple clang 21（libc++）；练习 4 另在 Ubuntu 24.04 的 GCC 13（libstdc++）上运行过，两者 `vector` 元素的析构顺序不同。
