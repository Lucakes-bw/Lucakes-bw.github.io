# 地铁换乘查询

《数据结构》专栏的实战项目：用 C 语言为一座虚构的城市写一个地铁查询程序。
项目说明和每个里程碑的要求见博客的「数据结构 → 实战项目」一章。

## 目录

```
Makefile          make 编译，make run 运行，make test 测试
src/              你的代码：main.c 已经写好，其余文件中标了 TODO 的地方需要你实现
data/metro.txt    示例城的地铁线路数据
tests/run.sh      测试脚本
tests/m1/         里程碑 1 的测试用例
```

## 常用命令

```bash
make                  # 编译出 build/metro
make run              # 用示例数据运行，输入 help 查看命令
tests/run.sh m1       # 运行里程碑 1 的测试
tests/run.sh m1 hash  # 只运行一组测试，分组见 tests/m1/groups
tests/run.sh -v m1    # 失败时显示完整的差异
```

测试脚本会用 `-fsanitize=address,undefined` 编译，越界、使用已释放的内存都会被当作失败；
在 Linux 上还会检查内存泄漏。
