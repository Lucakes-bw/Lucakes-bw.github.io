---
title: Linux 常用命令速查
date: 2026-10-21 10:00:00
updated: 2026-10-21 10:00:00
categories:
  - Linux基础
  - 常用命令
tags:
  - Linux
  - 命令
order: 1
description: 文件和目录操作、查看文件内容、权限管理、查找、进程管理等最常用的 Linux 命令。
cover: /img/covers/linux.svg
top_group_list: true
---

## 1. 目录与文件

| 命令 | 作用 | 示例 |
| --- | --- | --- |
| `pwd` | 显示当前目录 | `pwd` |
| `ls` | 列出目录内容 | `ls -lah` |
| `cd` | 切换目录 | `cd ~/code`、`cd ..`、`cd -` |
| `mkdir` | 创建目录 | `mkdir -p a/b/c` |
| `touch` | 创建空文件 / 更新时间戳 | `touch main.c` |
| `cp` | 复制 | `cp -r src/ backup/` |
| `mv` | 移动或重命名 | `mv old.c new.c` |
| `rm` | 删除 | `rm -r build/`（谨慎使用） |

## 2. 查看文件内容

```bash
cat main.c          # 输出整个文件
less big.log        # 分页查看，q 退出，/ 搜索
head -n 20 a.txt    # 前 20 行
tail -f app.log     # 持续查看文件末尾，常用于看日志
wc -l main.c        # 统计行数
```

## 3. 权限

`ls -l` 输出中的 `-rwxr-xr--` 依次表示：文件类型、所有者权限、所属组权限、其他用户权限。

```bash
chmod +x run.sh        # 添加可执行权限
chmod 644 config.ini   # rw-r--r--
chown user:group file  # 修改所有者和所属组
```

权限数字：`r=4`、`w=2`、`x=1`，相加得到每一组的值。

## 4. 查找

```bash
find . -name "*.c"              # 按文件名查找
grep -rn "main" src/            # 在目录中递归搜索文本，显示行号
which gcc                       # 查看命令所在路径
```

## 5. 进程

```bash
ps aux | grep nginx    # 查看进程
top                    # 实时查看系统资源
kill -9 1234           # 强制结束 PID 为 1234 的进程
```

## 6. 小结

记不住参数时，使用 `man 命令` 或 `命令 --help` 查看帮助。
