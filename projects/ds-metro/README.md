# 地铁换乘查询（数据结构专栏实战项目）

- `starter/`：发给学习者的起始代码、数据和测试，打包后就是 `source/downloads/ds-metro.zip`
- `solution/mN/src/`：每个里程碑完成后的参考实现
- `pack.sh`：修改 `starter/` 后重新打包

验证参考实现：

```bash
for k in 1 2 3 4 5; do projects/ds-metro/starter/tests/run.sh m$k projects/ds-metro/solution/m$k/src; done
```

`tests/mN/cases/*.out` 是用 `solution/m5` 生成的。修改了输出格式或测试输入以后，要重新生成并检查差异。
`tests/m2/data/grid.txt` 由同目录的 `gen-grid.sh 100` 生成。

这个目录不在 `source/` 中，Hexo 不会处理它；网站上只发布打包好的 zip。
