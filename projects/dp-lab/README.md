# dp-lab：设计模式专栏的配套代码

每篇文章一个目录（`02-uml/` 对应「基础与设计原则」的第 2 篇《UML 类图》），目录下每个 `.cpp` 是一个独立的程序。以 `_ce.cpp` 结尾的文件是故意编译不过的演示，CMake 不会构建它们。

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
| `01-why` | 为什么需要设计模式 | `report_if_v1~v3`（if-else 版的三个版本）、`report_if_bug`（漏改 printFooter）、`report_oop_v1~v3`（多态版的三个版本）、`report_oop_ce.cpp`（漏写 footer，编译失败）、`report_enum_bug`（练习 2）、`report_oop_md`（练习 3） |
| `03-solid1` | SOLID 原则（上） | `srp_shared`（共用函数引发的事故）、`srp_split`（按角色拆分）、`ocp_flags` / `ocp_rules`（密码规则）；`headers/measure.py`（头文件依赖的编译代价，`python3 headers/measure.py g++`）；`plugin/run.sh`（插件实验，需要 `dlopen`，CMake 不构建子目录） |
| `02-uml` | UML 类图 | `lifetime`（组合、聚合、关联的生命周期）、`lifetime_owned`（练习 4）、`player`（从代码画类图） |

文中的输出来自 macOS 上的 Apple clang 21（libc++）。另在 Ubuntu 24.04 的 GCC 13（libstdc++）上验证过：《UML 类图》练习 4（两者 `vector` 元素的析构顺序不同），《为什么需要设计模式》中 `report_if_bug` 开 `-Wall -Wextra` 没有警告、`report_enum_bug` 有 `-Wswitch` 警告。

《为什么需要设计模式》第 5 节的改动统计：`diff -U0 report_if_v1.cpp report_if_v2.cpp` 等，去掉第一行注释和 `main` 里的改动。
