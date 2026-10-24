---
title: 文本处理与管道
date: 2026-10-24 09:10:00
updated: 2026-10-24 09:10:00
categories:
  - Linux基础
  - 常用命令
tags:
  - Linux
  - 管道
  - grep
  - awk
order: 3
description: Linux 的哲学是「每个程序只做好一件事，用管道把它们组合起来」。本文介绍重定向、管道，以及 grep、cut、sort、uniq、wc、sed、awk 等文本处理命令，并用它们分析一份 2000 行的 Web 访问日志：找出访问最多的 IP、各种状态码的数量、可疑的扫描行为和最耗流量的页面。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：掌握输入输出重定向和管道；会用 `grep` 搜索、`cut` 和 `awk` 提取字段、`sort` 和 `uniq` 统计、`sed` 替换；能把这些命令组合起来分析日志。
>
> **前置知识**：《文件与目录操作》。
>
> **实验数据**：`~/logs/access.log`，一份模拟的 Web 服务器访问日志，共 2000 行，每行的格式是：

```
192.168.1.10 - - [23/Oct/2026:08:00:00 +0800] "GET /posts/linked-list/ HTTP/1.1" 200 33842 84ms
203.0.113.9 - - [23/Oct/2026:08:07:13 +0800] "GET /wp-login.php HTTP/1.1" 404 0 78ms
10.0.0.5 - - [23/Oct/2026:08:14:26 +0800] "GET /posts/ds-heap/ HTTP/1.1" 200 37823 72ms
```

用空格分隔的各个字段依次是：客户端 IP、两个占位符、时间（第 4、5 个字段）、请求方法、路径（第 7 个字段）、协议、状态码（第 9 个）、返回的字节数（第 10 个）、耗时（第 11 个）。

## 1. 标准输入输出和重定向

每个程序启动时都打开了三个「文件」：

| 编号 | 名称 | 默认连接到 |
|---|---|---|
| 0 | 标准输入（stdin） | 键盘 |
| 1 | 标准输出（stdout） | 屏幕 |
| 2 | 标准错误（stderr） | 屏幕 |

这和 C 语言中的 `stdin`、`stdout`、`stderr` 是同一回事（《输入与输出》）。**重定向**就是把它们连接到文件上：

| 写法 | 含义 |
|---|---|
| `命令 > 文件` | 标准输出写入文件（**覆盖**） |
| `命令 >> 文件` | 标准输出**追加**到文件末尾 |
| `命令 2> 文件` | 标准错误写入文件 |
| `命令 > 文件 2>&1` | 标准输出和标准错误都写入同一个文件 |
| `命令 < 文件` | 从文件读取标准输入 |

```bash
echo first > out.txt
echo second > out.txt
cat out.txt
echo third >> out.txt
cat out.txt
```

```
second
second
third
```

`>` 会先清空文件。把正常的输出和错误分开：

```bash
ls access.log nofile > ok.txt 2> err.txt
```

`ok.txt` 中是 `access.log`，`err.txt` 中是 `ls: cannot access 'nofile': No such file or directory`。

### 1.1 2>&1 的顺序

`2>&1` 的意思是「让 2 号指向 1 号**当前**指向的地方」。所以顺序很重要：

```bash
ls access.log nofile > all.txt 2>&1     # 先把 1 指向文件，再让 2 也指向文件
ls access.log nofile 2>&1 > all2.txt    # 先让 2 指向屏幕（1 当时的位置），再把 1 指向文件
```

第一条什么都没有输出到屏幕，`all.txt` 中两行都有；第二条把错误信息输出到了屏幕上，`all2.txt` 中只有 `access.log`。第 3.3 篇会用 `dup2` 系统调用自己实现重定向，到时就会彻底明白其中的原理。

### 1.2 /dev/null 和 tee

```bash
ls nofile 2>/dev/null
echo "exit=$?"
```

```
exit=2
```

`/dev/null` 是一个特殊的设备文件，写入它的内容都被丢弃。常用来忽略不关心的输出。

**`tee`** 把输入同时写到屏幕和文件，像一个「T 型接头」：

```bash
grep -c " 500 " access.log | tee count.txt
```

```
38
```

屏幕上显示了 38，`count.txt` 中也保存了一份。

### 1.3 陷阱：读写同一个文件

```bash
seq 3 > n.txt
sort -r n.txt > n.txt
wc -c n.txt
```

```
0 n.txt
```

想把文件倒序排序后写回去，结果文件变成了空的。原因是 Shell 在**启动 `sort` 之前**就处理了 `> n.txt`，把文件清空了，`sort` 读到的是一个空文件。正确的做法是先写到另一个文件，再改名：`sort -r n.txt > tmp && mv tmp n.txt`。

## 2. 管道

**管道** `|` 把前一个命令的标准输出，直接连接到后一个命令的标准输入：

```bash
命令1 | 命令2 | 命令3
```

数据在命令之间流动，不需要中间文件。这正是 Unix 的设计哲学：**每个程序只做好一件事，再用管道把它们组合起来完成复杂的任务**。

管道中的命令是**同时运行**的：`命令1` 产生一行，`命令2` 就可以处理一行。所以 `tail -f access.log | grep 500` 能实时地显示新出现的错误。

一个细节：管道的退出码是**最后一个命令**的退出码。

```bash
false | true
echo "exit=$?"
set -o pipefail
false | true
echo "pipefail exit=$?"
```

```
exit=0
pipefail exit=1
```

`false` 失败了，但整个管道的退出码是 0。在脚本中通常要 `set -o pipefail`，让管道中任何一个命令失败都能被发现（第 2.4 篇 Shell 脚本中会用到）。

## 3. 文本处理命令

### 3.1 grep：搜索

```bash
grep -c " 404 " access.log                   # 统计匹配的行数
grep "wp-login" access.log | head -2
grep -v " 200 " access.log | wc -l           # -v：不匹配的行
grep -E " (403|500) " access.log | head -2   # -E：扩展正则表达式
grep -n "/admin" access.log | head -2        # -n：显示行号
```

```
357
203.0.113.9 - - [23/Oct/2026:08:07:13 +0800] "GET /wp-login.php HTTP/1.1" 404 0 78ms
198.51.100.7 - - [23/Oct/2026:08:49:31 +0800] "GET /wp-login.php HTTP/1.1" 404 0 50ms
546
203.0.113.9 - - [23/Oct/2026:08:45:15 +0800] "GET /admin HTTP/1.1" 403 0 67ms
203.0.113.9 - - [23/Oct/2026:08:34:46 +0800] "GET /admin HTTP/1.1" 403 0 53ms
16:203.0.113.9 - - [23/Oct/2026:08:45:15 +0800] "GET /admin HTTP/1.1" 403 0 67ms
23:203.0.113.9 - - [23/Oct/2026:08:34:46 +0800] "GET /admin HTTP/1.1" 403 0 53ms
```

| 选项 | 作用 |
|---|---|
| `-i` | 不区分大小写 |
| `-v` | 反向：显示不匹配的行 |
| `-c` | 只输出匹配的行数 |
| `-n` | 显示行号 |
| `-E` | 使用扩展正则表达式（`\|`、`+`、`()` 等） |
| `-r` | 递归搜索目录中的所有文件 |
| `-o` | 只输出匹配的部分 |

注意 `" 404 "` 前后的空格：如果只搜索 `404`，返回字节数或耗时中恰好包含 404 的行也会被匹配。

`grep -r "TODO" src/` 在整个源代码目录中搜索，是写代码时最常用的命令之一。

### 3.2 cut：按列提取

```bash
head -3 access.log | cut -d" " -f1
```

```
192.168.1.10
203.0.113.9
10.0.0.5
```

`-d` 指定分隔符，`-f` 指定第几个字段（可以是 `-f1,9` 或 `-f1-3`）。`cut` 要求分隔符是单个字符，连续的多个空格会被当作多个空字段，这时用 `awk` 更方便。

### 3.3 sort、uniq、wc：排序和统计

```bash
printf "b\na\nb\nb\na\n" | uniq
printf "b\na\nb\nb\na\n" | sort | uniq -c
```

```
b
a
b
a
      2 a
      3 b
```

**`uniq` 只能去掉相邻的重复行**，所以几乎总是和 `sort` 一起用。`uniq -c` 在每行前面加上出现的次数。

`sort` 常用的选项：`-n` 按数值排序（否则 `10` 会排在 `9` 前面），`-r` 逆序，`-k 2` 按第 2 列排序，`-u` 去重。`wc` 统计行数（`-l`）、单词数（`-w`）、字节数（`-c`）。

把它们组合起来，**统计访问次数最多的 IP**：

```bash
cut -d" " -f1 access.log | sort | uniq -c | sort -nr | head -5
```

```
    467 203.0.113.9
    382 192.168.1.10
    286 10.0.0.5
    262 192.168.1.11
    188 192.168.1.12
```

这个 `sort | uniq -c | sort -nr | head` 的组合是统计「出现次数最多的前几名」的标准写法，非常值得记住。一步一步地看：`cut` 取出 IP；`sort` 把相同的 IP 排到一起；`uniq -c` 统计每个 IP 出现的次数；`sort -nr` 按次数从大到小排序；`head -5` 取前 5 名。

### 3.4 sed：替换

`sed` 是「流编辑器」，最常用的是 `s/旧/新/` 替换：

```bash
head -2 access.log | sed "s/HTTP\/1.1/HTTP\/2/"
head -1 access.log | sed -E "s/[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+/x.x.x.x/"
sed -n "2,3p" access.log
```

```
192.168.1.10 - - [23/Oct/2026:08:00:00 +0800] "GET /posts/linked-list/ HTTP/2" 200 33842 84ms
203.0.113.9 - - [23/Oct/2026:08:07:13 +0800] "GET /wp-login.php HTTP/2" 404 0 78ms
x.x.x.x - - [23/Oct/2026:08:00:00 +0800] "GET /posts/linked-list/ HTTP/1.1" 200 33842 84ms
203.0.113.9 - - [23/Oct/2026:08:07:13 +0800] "GET /wp-login.php HTTP/1.1" 404 0 78ms
10.0.0.5 - - [23/Oct/2026:08:14:26 +0800] "GET /posts/ds-heap/ HTTP/1.1" 200 37823 72ms
```

- `s/旧/新/` 只替换每行的第一个匹配，`s/旧/新/g` 替换所有的匹配。
- 模式中的 `/` 要写成 `\/`；也可以换一个分隔符：`s|HTTP/1.1|HTTP/2|`。
- 第二条用正则表达式把 IP 地址替换掉，可以在分享日志之前隐藏用户的 IP。
- `sed -n "2,3p"` 只打印第 2 到 3 行。
- `sed -i "s/旧/新/g" 文件` 直接修改文件（`-i` 表示 in-place）。**修改之前最好先不带 `-i` 运行一次，确认结果正确。**

### 3.5 awk：按字段处理

`awk` 是一门小型的编程语言，特别适合处理按列排列的文本。它会自动把每一行按空白拆分成 `$1`、`$2`……（`$0` 是整行）：

```bash
awk '{print $9}' access.log | sort | uniq -c | sort -nr
```

```
   1454 200
    357 404
    151 403
     38 500
```

awk 的程序由「`条件 {动作}`」组成，`END {}` 中的动作在处理完所有行之后执行。**统计成功请求返回的总字节数**：

```bash
awk '$9 == 200 {sum += $10} END {print sum, "字节", sum/1024/1024, "MB"}' access.log
```

```
34466458 字节 32.8698 MB
```

**统计耗时超过 1 秒的请求**（第 11 个字段是 `84ms` 这样的字符串，先去掉 `ms`）：

```bash
awk '{t = $11; sub(/ms/, "", t); if (t + 0 > 1000) n++} END {print n}' access.log
```

```
35
```

**统计每个小时的请求数**（`split` 按 `:` 把时间字段拆开，第 2 部分是小时）：

```bash
awk '{split($4, a, ":"); print a[2]}' access.log | sort | uniq -c | head -4
```

```
    143 08
    143 09
    143 10
    143 11
```

awk 的程序要用**单引号**括起来，否则 `$9` 会被 Shell 当作自己的变量展开（通常是空的）。

## 4. 综合：发现可疑的访问

正常的用户很少访问不存在的页面。统计返回 403 和 404 最多的 IP：

```bash
grep -E " (403|404) " access.log | cut -d" " -f1 | sort | uniq -c | sort -nr | head -3
```

```
    302 203.0.113.9
     44 192.168.1.10
     44 10.0.0.5
```

`203.0.113.9` 产生了 302 次 403 和 404，远远多于其他人。看看它访问了什么：

```bash
grep "^203.0.113.9 " access.log | awk '{print $7}' | sort | uniq -c | sort -nr | head -4
```

```
    110 /wp-login.php
    107 /admin
     74 /.env
     53 /index.html
```

它反复访问 `/wp-login.php`、`/admin`、`/.env`，这是典型的**漏洞扫描**：自动化的工具在尝试 WordPress 的登录页、管理后台，以及可能泄露密码的配置文件。实际中可以把这样的 IP 加入防火墙的黑名单。

一行命令就完成了一次简单的安全分析。这就是管道的威力：**每个命令都很简单，组合起来却能解决实际问题**。

## 5. xargs：把输出变成参数

管道把输出连接到下一个命令的**标准输入**，但有些命令（比如 `rm`）不读标准输入，而是需要**命令行参数**。`xargs` 把标准输入的内容转换成参数：

```bash
touch t1.tmp t2.tmp t3.tmp
ls *.tmp | xargs rm -v
```

```
removed 't1.tmp'
removed 't2.tmp'
removed 't3.tmp'
```

常见的组合是 `find ... | xargs ...`。如果文件名中可能有空格，要写成 `find . -name "*.tmp" -print0 | xargs -0 rm`，用空字符而不是空格分隔文件名。

## 6. 常见错误

1. **用 `>` 覆盖了重要的文件**：可以在 `.bashrc` 中加上 `set -o noclobber`，禁止 `>` 覆盖已存在的文件。
2. **读写同一个文件**：`sort f > f` 得到空文件。
3. **`2>&1` 写在 `> 文件` 前面**：错误信息仍然输出到屏幕。
4. **`uniq` 之前没有 `sort`**：只去掉了相邻的重复。
5. **`sort` 数字时忘了 `-n`**：按字符串排序，`10` 排在 `9` 前面。
6. **awk 程序用双引号**：`$1` 被 Shell 展开。
7. **`sed -i` 没有先预览**：直接修改了文件。

## 7. 小结

- 0、1、2 号分别是标准输入、标准输出、标准错误；`>` 覆盖，`>>` 追加，`2>` 重定向错误，`2>&1` 的顺序有讲究
- 管道 `|` 把前一个命令的输出连接到后一个命令的输入，命令同时运行；`set -o pipefail` 让管道中的失败不被忽略
- `grep` 搜索，`cut` 和 `awk` 取列，`sort | uniq -c | sort -nr | head` 统计排名，`sed` 替换，`xargs` 把输出变成参数
- 实测用管道分析 2000 行日志：找出了访问最多的 IP、各状态码的数量、总流量，以及一个进行漏洞扫描的 IP

## 8. 练习

1. 日志中一共有多少个不同的 IP？
2. 500 错误最多出现在哪个小时？
3. 哪 3 个路径消耗的流量（返回字节数之和）最多？

<details>
<summary>点击查看答案</summary>

1. `cut -d" " -f1 access.log | sort -u | wc -l`，结果是 10。`sort -u` 相当于 `sort | uniq`。

2. 先筛选出 500 的行，再按小时统计：

   ```bash
   grep " 500 " access.log | awk '{split($4, a, ":"); print a[2]}' | sort | uniq -c | sort -nr | head -3
   ```

   ```
         6 16
         5 15
         5 13
   ```

   16 点最多，有 6 次。

3. 用 awk 的**关联数组**按路径累加字节数：

   ```bash
   awk '{bytes[$7] += $10} END {for (p in bytes) print bytes[p], p}' access.log | sort -nr | head -3
   ```

   ```
   10627183 /index.html
   6624354 /posts/ds-heap/
   4992931 /js/dsviz.js
   ```

   `bytes[$7]` 以路径为键，awk 的数组可以用字符串作为下标，就像 C++ 的 `std::map`。`for (p in bytes)` 的顺序是不确定的，所以最后要排序。

</details>
