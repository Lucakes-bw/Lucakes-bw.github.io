# minilog：设计模式专栏第六章实战项目

零外部依赖的现代 C++17 轻量日志引擎，作为《设计模式》专栏第六章（6.1～6.4）的配套实验与压测工程。

## 目录结构

- `include/minilog/`：公开头文件（`logger.h` 采用严格 Pimpl 防火墙，不暴露 `<fstream>` / `<mutex>` / `<thread>`）
- `src/`：格式预编译、过滤脱敏管线、NVI `BaseSink`、滚动策略、`AsyncWorker`、Pimpl 实现
- `legacy/`：对照用大泥球全局单例版（`monolithic_logger`）、6 模块编译防火墙对比工程（`compile_bench`）与过度 GoF 炫技版（`overengineered_logger.cpp`）
- `tests/`：零依赖单元测试（覆盖参数校验、注册表隔离、RAII 刷盘、过滤脱敏、滚动归档、SBO `AnySink` 与多线程异步排空）
- `bench/`：对应 6.2、6.3、6.4 的性能与堆分配实测程序

## 构建与复现

```bash
cmake -B build && cmake --build build -j4
ctest --test-dir build --output-on-failure
```

一键跑完 Release 测试、ASan/UBSan、TSan 与全部 6.1～6.4 实测：

```bash
bash measure.sh
```
