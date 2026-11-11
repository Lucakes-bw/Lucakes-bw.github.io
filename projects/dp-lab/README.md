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
| `11-adapter` | 适配器模式 | `adapters`（类适配器、推转拉适配器）、`stl_adapters`（标准库里的适配器）；`ffmpeg/build.sh`（FFmpeg 对象适配器 `FfmpegSource`、直接用 C API 的 `raw_c`、通过适配器的 `early_stop`，需要 FFmpeg 开发库；用 `leaks --atExit -- ./build/raw_c bbb-10s.mp4` 检查泄漏；Linux 上可以加 `-fsanitize=address` 编译，用 AddressSanitizer 检查） |
| `12-bridge` | 桥接模式 | `inherit_v1~v3` / `bridge_v1~v3`（继承与桥接：3 种视图 × 3 种后端，再加一种视图、一种后端；`sh measure.sh` 统计类的个数和改动的行数）、`bench_dispatch`（间接调用和接口粒度的开销，请用 `-O2` 编译）、`template_bridge`（编译期的桥接）、`pimpl`（只有一种实现的桥接）、`ex2_snapshot` / `ex3_shared`（练习 2、3）；`sdl/run.sh`（同一段绘制代码跑在 SDL 的每个渲染驱动上，需要 SDL3） |
| `13-composite` | 组合模式 | `if_folder`（叶子和容器分开处理，漏改一处算错大小）、`composite`（重构成组合模式）、`transparent` / `safe_ce.cpp`（透明与安全，后者编译失败）、`du_tree`（用 `std::filesystem` 统计真实目录：`./build/13-composite/du_tree ../../source`）、`clone_tree`（深拷贝）、`deep_tree`（很深的树：`./deep_tree 200000`、`./deep_tree 20000 nosize`，看在哪一步栈溢出）、`deep_fixed`（显式栈和非递归析构，请用 `-O2` 编译）、`mp4_tree`（MP4 的 box 树：`./mp4_tree bbb-10s.mp4`）、`ex2_large_files` / `ex3_iterative_print` / `ex4_assign`（练习 2～4） |
| `14-decorator` | 装饰器模式 | `inherit_v1~v2` / `decorator_v1~v2`（继承与装饰器：日志、校验和、统计三种功能，再加一种「丢掉坏包」；`sh measure.sh` 统计类的个数和改动的行数）、`order`（包装顺序：先放大再限幅和先限幅再放大）、`bench_layers`（套 N 层的调用开销：虚函数、模板 mixin、`std::function`，请用 `-O2` 编译）、`mixin`（模板 mixin）、`lambda_decor`（`std::function` 和 lambda 写的装饰器）、`ex2_limit` / `ex3_stats_access` / `ex4_identity`（练习 2～4）；`ffmpeg/build.sh`（给 FFmpeg 的 `AVIOContext` 套两层计数装饰器，比较缓冲区放在外层和内层，需要 FFmpeg 开发库） |
| `15-facade` | 外观模式 | `no_facade`（每个调用方自己按顺序调 15 个子系统函数：拿错帧、漏关句柄）、`facade`（`Thumbnailer` 外观）、`bypass`（外观不禁止直接用子系统；复用外观对象）、`ex2_grab_many` / `ex4_copy_bug` / `ex4_fixed` / `ex4_copy_ce.cpp`（练习 2、4，最后一个编译失败）；`ffmpeg/build.sh`（需要 FFmpeg）：`raw_thumb`（直接调 FFmpeg 的 C API 存缩略图）、`thumb`（只链接外观库 `libthumbnailer`）、`sheet`（3×3 缩略图墙）、`bench`（外观的开销）、`measure.sh`（调用方的代码行数、FFmpeg 函数数、编译时间） |
| `16-flyweight` | 享元模式 | `naive`（每个字一个完整的 Glyph：44 MB）、`flyweight`（`GlyphFactory` 共享字形：5.6 MB）、`bench`（排版、整轨绘制、工厂查找的耗时）、`interning`（滤镜描述的字符串驻留）、`weak_cache`（`shared_ptr` / `weak_ptr` 缓存）、`mutable_bug` / `mutable_ce.cpp`（把外部状态放进享元；改成 const 后编译失败）、`mt_factory`（多线程下工厂加锁的代价）、`ex2_lru` / `ex3_style` / `ex4_vector_bug` / `ex4_fixed`（练习 2～4） |
| `17-proxy` | 代理模式 | `no_proxy` / `virtual_proxy`（120 个缩略图全部解码 vs 虚代理：解码次数和堆内存）、`bench_lazy`（启动耗时和每次调用多绕一层的开销，请用 `-O2`）、`protection_proxy`（保护代理：按角色拒绝并记审计日志）、`cache_proxy`（LRU 缓存代理在不同容量下的命中率）、`remote_proxy`（远程代理：逐个调用和粗粒度接口的往返次数）、`smart_ptr_proxy`（`shared_ptr` 和 `operator->` 链）、`vector_bool` / `vector_bool_ce.cpp`（`vector<bool>::reference` 代理对象和 `auto`，后者编译失败）、`lazy_template`（通用的 `Lazy<T>`）、`ex2_unload` / `ex3_order` / `ex4_bug` / `ex4_fixed`（练习 2～4）；`sh measure.sh` 用 `-O2` 跑一遍实测；`ffmpeg/build.sh` + `ffmpeg/run.sh`（需要 FFmpeg）：`lazy_strip`（真正解码 20 张时间轴缩略图：全部解码 vs 虚代理）、`probe_cache`（用 FFmpeg 读流信息做真实对象，缓存代理每次请求的平均耗时） |
| `18-strategy` | 策略模式 | `switch_scaler` / `switch_scaler_lanczos`（enum + switch 选缩放算法，加 Lanczos 前后）、`strategy_scaler` / `strategy_scaler_lanczos`（重构成策略，加 Lanczos 前后）、`quality`（读一帧 PGM，四种策略 720p→1080p 的耗时和缩小再放大的 PSNR，请用 `-O2`）、`runtime_switch`（播放器掉帧时自动降级、恢复）、`modern`（虚函数、`std::function`、模板参数、`variant` 四种写法）、`bench_granularity`（每像素调用策略和每帧调用一次的开销，请用 `-O2`）、`sizes`（上下文对象的大小、`unique_ptr` 删除器、`std::function` 的堆分配）、`ex2_area` / `ex3_rate_control` / `ex4_bug` / `ex4_fixed`（练习 2～4）；`sh measure.sh` 统计加一种算法要改几处，并用 `-O2` 跑一遍实测（需要 `ffmpeg` 取测试帧，或用 `FRAME=` 指定） |
| `02-uml` | UML 类图 | `lifetime`（组合、聚合、关联的生命周期）、`lifetime_owned`（练习 4）、`player`（从代码画类图） |

文中的输出来自 macOS 上的 Apple clang 21（libc++）。另在 Ubuntu 24.04 的 GCC 13（libstdc++）上验证过：《UML 类图》练习 4（两者 `vector` 元素的析构顺序不同），《为什么需要设计模式》中 `report_if_bug` 开 `-Wall -Wextra` 没有警告、`report_enum_bug` 有 `-Wswitch` 警告。

《为什么需要设计模式》第 5 节的改动统计：`diff -U0 report_if_v1.cpp report_if_v2.cpp` 等，去掉第一行注释和 `main` 里的改动。
