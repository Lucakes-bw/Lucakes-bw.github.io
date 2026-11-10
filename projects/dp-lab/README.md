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
| `04-solid2` | SOLID 原则（下） | `lsp_square` / `lsp_fixed`（正方形与长方形）、`lsp_vector_bool_ce.cpp`（练习 1，编译失败）、`dip_before` / `dip_after` / `dip_measure`（依赖倒置与测试耗时）；`isp/measure.sh`（接口隔离：改 `fax()` 后重新编译的文件，需要 CMake 和 make） |
| `05-composition` | 组合优于继承与 C++ 的四种多态 | `fragile_base` / `fragile_fixed`（脆弱基类与组合）、`four_ways`（四种多态写法）、`bench`（性能实测，请用 `-O2` 编译，CMake 默认的 Debug 构建不适合测性能）、`variant_missing_ce.cpp`（练习 3，编译失败） |
| `06-singleton` | 单例模式 | `race_naive` / `race_meyers`（8 个线程同时第一次访问）、`bench_access`（五种写法的访问开销，请用 `-O2 -pthread` 编译）、`singleton_basic`、`test_order` / `test_di`（单例与依赖注入的测试）；`initorder/run.sh`（静态初始化顺序）；`dup/run.sh`（动态库里的重复实例） |
| `07-factory` | 简单工厂与工厂方法 | `simple_factory`、`factory_method`、`probe_registry`（单文件版）；`registry/run.sh`（多文件的注册表工厂：用 ffmpeg 从测试视频提取样本，比较直接链接、静态库、强制链接整个静态库三种构建方式）；`registry/exercise/flv_parser.cpp`（练习 3） |
| `08-abstract-factory` | 抽象工厂 | `mix_bug`（分别配置，9 种组合只有 3 种能用）、`abstract_factory`（按运行环境选后端）、`family_templates`（编译期产品族）、`family_mix_ce.cpp`（混用两族，编译失败） |
| `09-builder` | 建造者模式 | `telescoping`（8 个参数的构造函数）、`builder`（链式建造者）、`director`（GoF 指挥者 + 两种建造者）、`designated`（C++20 指定初始化，CMake 自动用 C++20 编译）、`typestate`（类型状态建造者）；`encode/run_encode.cpp`（把生成的命令交给 ffmpeg 执行，单独编译：`clang++ -std=c++17 -O2 encode/run_encode.cpp -o run_encode`） |
| `10-prototype` | 原型模式 | `slicing`（对象切片与 dynamic_cast）、`clone_basic`、`forgot_clone`（孙类忘了重写 clone）、`crtp_clone`、`shallow_deep`（浅拷贝与深拷贝）、`prototype_registry`（原型注册表）、`bench_clone`（克隆与重新解析 3D LUT 的耗时，请用 `-O2` 编译；加 `--sscanf` 复现慢写法） |
| `11-adapter` | 适配器模式 | `adapters`（类适配器、推转拉适配器）、`stl_adapters`（标准库里的适配器）；`ffmpeg/build.sh`（FFmpeg 对象适配器 `FfmpegSource`、直接用 C API 的 `raw_c`、通过适配器的 `early_stop`，需要 FFmpeg 开发库；用 `leaks --atExit -- ./build/raw_c bbb-10s.mp4` 检查泄漏） |
| `02-uml` | UML 类图 | `lifetime`（组合、聚合、关联的生命周期）、`lifetime_owned`（练习 4）、`player`（从代码画类图） |

文中的输出来自 macOS 上的 Apple clang 21（libc++）。另在 Ubuntu 24.04 的 GCC 13（libstdc++）上验证过：《UML 类图》练习 4（两者 `vector` 元素的析构顺序不同），《为什么需要设计模式》中 `report_if_bug` 开 `-Wall -Wextra` 没有警告、`report_enum_bug` 有 `-Wswitch` 警告。

《为什么需要设计模式》第 5 节的改动统计：`diff -U0 report_if_v1.cpp report_if_v2.cpp` 等，去掉第一行注释和 `main` 里的改动。
