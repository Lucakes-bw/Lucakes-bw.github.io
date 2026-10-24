---
title: 文件与目录操作
date: 2026-10-21 10:00:00
updated: 2026-10-24 09:05:00
categories:
  - Linux基础
  - 常用命令
tags:
  - Linux
  - 命令
  - 文件系统
order: 2
description: 创建、复制、移动、删除文件和目录，查看文件内容，用通配符和 find 查找文件，是使用 Linux 最基本的功夫。本文在 Ubuntu 中实测这些命令，并通过 inode 编号讲清楚硬链接和软链接的本质区别：删除原文件后，硬链接仍然能读取内容，软链接却失效了。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：熟练使用 `ls`、`cp`、`mv`、`rm`、`mkdir`、`touch` 等命令；会查看文件内容；会用通配符和 `find` 查找文件；理解 inode，分清硬链接和软链接。
>
> **前置知识**：《认识 Linux 与 Shell》。
>
> **实验环境**：Ubuntu 24.04，用户 `lucky`。实验在 `~/demo` 等目录中进行（容器的时区是 UTC，所以文件时间显示为凌晨）。

## 1. 查看目录：ls

```bash
touch a.txt
echo "hello" > notes.txt
mkdir -p docs/old
ls
ls -l
```

```
a.txt
docs
notes.txt
total 8
-rw-r--r-- 1 lucky lucky    0 Oct 24 01:52 a.txt
drwxr-xr-x 3 lucky lucky 4096 Oct 24 01:52 docs
-rw-r--r-- 1 lucky lucky    6 Oct 24 01:52 notes.txt
```

- `touch` 创建一个空文件（如果文件已经存在，就更新它的修改时间）；`echo "hello" > notes.txt` 把文字写入文件（`>` 是重定向，第 1.3 篇介绍）。
- `ls -l` 以**长格式**显示，每一列的含义：

```
-rw-r--r--  1      lucky  lucky  6     Oct 24 01:52  notes.txt
权限        链接数  所有者  所属组  大小  修改时间      文件名
```

第一个字符表示**类型**：`-` 是普通文件，`d` 是目录，`l` 是符号链接。后面 9 个字符是权限（第 1.4 篇详细讲解）。

常用的选项：

| 选项 | 作用 |
|---|---|
| `-l` | 长格式 |
| `-a` | 显示隐藏文件（以 `.` 开头的） |
| `-h` | 大小用 K、M、G 显示，配合 `-l` 使用 |
| `-t` | 按修改时间排序，最新的在前 |
| `-S` | 按大小排序 |
| `-R` | 递归显示子目录 |
| `-d` | 显示目录本身，而不是它的内容 |
| `-i` | 显示 inode 编号（第 6 节） |

```bash
ls -lh /usr/bin/aarch64-linux-gnu-gcc-13
ls -ld docs
```

```
-rwxr-xr-x 1 root root 967K Dec 18  2025 /usr/bin/aarch64-linux-gnu-gcc-13
drwxr-xr-x 3 lucky lucky 4096 Oct 24 01:52 docs
```

`ls -l` 显示的目录大小（4096）是目录本身占用的空间，不是目录中所有文件的总大小。要统计一个目录占用的总空间，用 `du -sh 目录`。

## 2. 创建、复制、移动、删除

### 2.1 创建

```bash
mkdir dir               # 创建目录
mkdir -p a/b/c          # 一次创建多级目录，已存在也不报错
touch file.txt          # 创建空文件
```

### 2.2 复制：cp

```bash
cp notes.txt notes.bak
cp -r docs docs2
cp docs docs3
```

```
cp: -r not specified; omitting directory 'docs'
```

复制目录必须加 `-r`（递归）。其他常用的选项：`-i` 覆盖之前询问，`-p` 保留权限和时间，`-a` 归档模式（保留所有属性，常用于备份）。

**注意：`cp` 默认会直接覆盖已经存在的目标文件，不会有任何提示。**

### 2.3 移动和重命名：mv

```bash
mv notes.bak docs/      # 移动到目录中
mv a.txt b.txt          # 重命名
```

Linux 中没有单独的「重命名」命令，重命名就是在同一个目录中移动。`mv` 同样会直接覆盖已经存在的目标，`-i` 让它先询问，`-n` 让它不覆盖。

### 2.4 删除：rm

```bash
rm b.txt
rm docs
rmdir docs
rm -r docs2
```

```
rm: cannot remove 'docs': Is a directory
rmdir: failed to remove 'docs': Directory not empty
```

- `rm` 删除文件；删除目录要加 `-r`。
- `rmdir` 只能删除**空**目录，比较安全。
- `rm -i` 删除前逐个询问：

```bash
rm -i x
```

```
rm: remove regular empty file 'x'?
```

**Linux 的命令行没有回收站，`rm` 删除的文件无法恢复。** 尤其要小心 `rm -rf`（`-f` 表示强制、不提示）：

- 执行前先用 `ls` 看看要删除的是什么；
- 不要在脚本中写 `rm -rf $DIR/*`：如果变量 `DIR` 碰巧是空的，就变成了 `rm -rf /*`；
- 永远不要以 root 身份执行自己不完全理解的 `rm` 命令。

## 3. 查看文件内容

| 命令 | 作用 |
|---|---|
| `cat 文件` | 显示整个文件，适合小文件 |
| `less 文件` | 分页查看，空格翻页、`/` 搜索、`q` 退出；适合大文件 |
| `head -n 10 文件` | 显示前 10 行（`-n` 可以省略，写成 `head -10`） |
| `tail -n 10 文件` | 显示最后 10 行 |
| `tail -f 文件` | 持续显示文件新增的内容，常用来实时查看日志，Ctrl+C 退出 |
| `wc -l 文件` | 统计行数 |

```bash
seq 1 100 > nums.txt
head -3 nums.txt
tail -2 nums.txt
wc -l nums.txt
```

```
1
2
3
99
100
100 nums.txt
```

（`seq 1 100` 输出 1 到 100，每行一个。）

## 4. 通配符

Shell 在执行命令之前，会把带有**通配符**的参数展开成匹配的文件名：

| 通配符 | 含义 |
|---|---|
| `*` | 任意多个任意字符（包括零个） |
| `?` | 一个任意字符 |
| `[abc]` | 方括号中的任意一个字符；`[a-z]` 表示范围 |

```bash
touch r1.c r2.c r3.h main.c
ls *.c
ls r?.*
ls [rm]*.c
echo *.cpp
```

```
main.c
r1.c
r2.c
r1.c
r2.c
r3.h
main.c
r1.c
r2.c
*.cpp
```

注意最后一个：没有任何文件匹配 `*.cpp` 时，bash 会**原样保留**这个字符串。这是 Shell 在做展开，而不是 `ls` 或 `echo`：`ls` 收到的参数已经是展开后的文件名列表了。

## 5. 查找文件：find

`find 起始目录 条件` 在目录树中递归地查找文件：

```bash
find . -name "*.c"
find . -type d
find /usr/include -name "stdio.h"
find . -name "*.txt" -size +100c
```

```
./r2.c
./main.c
./r1.c
.
./docs
./docs/old
/usr/include/stdio.h
/usr/include/aarch64-linux-gnu/bits/stdio.h
/usr/include/c++/13/tr1/stdio.h
./nums.txt
```

| 条件 | 含义 |
|---|---|
| `-name "模式"` | 按文件名匹配（区分大小写；`-iname` 不区分） |
| `-type f` / `-type d` | 普通文件 / 目录 |
| `-size +100c` | 大于 100 字节（`k`、`M`、`G` 表示 KB、MB、GB） |
| `-mtime -7` | 最近 7 天内修改过 |
| `-newer 文件` | 比某个文件更新 |

**`-name` 后面的模式一定要加引号**。否则 `*.c` 会先被 Shell 展开成当前目录中的文件名，`find` 收到的就不是通配符了。

`find` 还可以对找到的每个文件执行命令，比如删除所有的 `.o` 文件：`find . -name "*.o" -delete`，或者 `find . -name "*.o" -exec rm {} \;`。

## 6. 硬链接和软链接

### 6.1 inode

Linux 的文件系统中，一个文件分为两部分：

- **inode**（索引节点）：保存文件的**元数据**（大小、权限、所有者、时间、数据存放在磁盘的什么位置），每个 inode 有一个编号；
- **目录项**：目录本质上是一张「文件名 → inode 编号」的表。

**文件名并不是文件的一部分**，它只是目录中指向 inode 的一个条目。理解了这一点，链接就很容易理解了。

### 6.2 实验

```bash
echo "v1" > original.txt
ln original.txt hard.txt          # 硬链接
ln -s original.txt soft.txt       # 软链接（符号链接）
ls -li
```

```
total 8
1066915 -rw-r--r-- 2 lucky lucky  3 Oct 24 01:52 hard.txt
1066915 -rw-r--r-- 2 lucky lucky  3 Oct 24 01:52 original.txt
1066936 lrwxrwxrwx 1 lucky lucky 12 Oct 24 01:52 soft.txt -> original.txt
```

- `hard.txt` 和 `original.txt` 的 inode 编号**完全相同**（1066915），链接数都是 **2**。它们是同一个文件的两个名字，没有主次之分。
- `soft.txt` 有自己的 inode（1066936），类型是 `l`，大小是 12 字节：它的内容就是字符串 `original.txt`，正好 12 个字符。

<figure class="diagram">
<svg viewBox="0 0 680 210" role="img" aria-label="硬链接和软链接示意图：original.txt 和 hard.txt 两个目录项指向同一个 inode；soft.txt 指向另一个 inode，它的内容是路径 original.txt">
  <defs>
    <marker id="ln-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker>
  </defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="20" y="22" style="fill:var(--muted)">目录项（文件名 → inode）</text>
    <text x="300" y="22" style="fill:var(--muted)">inode</text>
    <text x="520" y="22" style="fill:var(--muted)">数据</text>
    <rect x="20" y="36" width="190" height="34" rx="5" style="fill:var(--panel-2);stroke:var(--border-2)"/>
    <text x="32" y="58" style="fill:var(--fg)">original.txt → 1066915</text>
    <rect x="20" y="86" width="190" height="34" rx="5" style="fill:var(--panel-2);stroke:var(--border-2)"/>
    <text x="32" y="108" style="fill:var(--fg)">hard.txt     → 1066915</text>
    <rect x="20" y="150" width="190" height="34" rx="5" style="fill:var(--panel-2);stroke:var(--border-2)"/>
    <text x="32" y="172" style="fill:var(--fg)">soft.txt     → 1066936</text>
    <rect x="290" y="56" width="170" height="48" rx="5" style="fill:var(--code-bg);stroke:var(--accent)"/>
    <text x="300" y="76" style="fill:var(--accent)">inode 1066915</text>
    <text x="300" y="95" style="fill:var(--fg)">链接数 2，3 字节</text>
    <rect x="290" y="146" width="170" height="44" rx="5" style="fill:var(--code-bg);stroke:var(--warn)"/>
    <text x="300" y="165" style="fill:var(--warn)">inode 1066936</text>
    <text x="300" y="182" style="fill:var(--fg)">类型：符号链接</text>
    <rect x="510" y="62" width="150" height="36" rx="5" style="fill:var(--panel);stroke:var(--border-2)"/>
    <text x="522" y="85" style="fill:var(--fg)">"v1\n"</text>
    <rect x="510" y="150" width="150" height="36" rx="5" style="fill:var(--panel);stroke:var(--border-2)"/>
    <text x="522" y="173" style="fill:var(--fg)">"original.txt"</text>
    <line x1="210" y1="53" x2="288" y2="72" style="stroke:var(--fg-2)" marker-end="url(#ln-arrow)"/>
    <line x1="210" y1="103" x2="288" y2="88" style="stroke:var(--fg-2)" marker-end="url(#ln-arrow)"/>
    <line x1="210" y1="167" x2="288" y2="167" style="stroke:var(--fg-2)" marker-end="url(#ln-arrow)"/>
    <line x1="460" y1="80" x2="508" y2="80" style="stroke:var(--fg-2)" marker-end="url(#ln-arrow)"/>
    <line x1="460" y1="168" x2="508" y2="168" style="stroke:var(--fg-2)" marker-end="url(#ln-arrow)"/>
    <path d="M585,150 C585,120 140,128 120,72" style="fill:none;stroke:var(--warn);stroke-dasharray:4 3" marker-end="url(#ln-arrow)"/>
    <text x="330" y="128" style="fill:var(--warn);font-size:12px">按路径名查找</text>
  </g>
</svg>
</figure>

通过硬链接修改，从另外两个名字都能看到：

```bash
echo "v2" >> hard.txt
cat original.txt
cat soft.txt
```

```
v1
v2
v1
v2
```

关键的区别在删除原文件之后：

```bash
rm original.txt
ls -li
cat hard.txt
cat soft.txt
```

```
total 4
1066915 -rw-r--r-- 1 lucky lucky  6 Oct 24 01:52 hard.txt
1066936 lrwxrwxrwx 1 lucky lucky 12 Oct 24 01:52 soft.txt -> original.txt
v1
v2
cat: soft.txt: No such file or directory
```

- `rm original.txt` 只是删除了一个**目录项**，inode 的链接数从 2 减到 1。只有链接数减到 0（而且没有进程打开它），文件的数据才真正被释放。所以 `hard.txt` 仍然完好。
- `soft.txt` 保存的是**路径** `original.txt`，这个路径已经不存在了，软链接**失效**了（叫作「悬空链接」）。

如果再创建一个同名的文件，软链接又能用了，但它指向的是一个全新的文件（inode 1067438）：

```bash
echo "new" > original.txt
cat soft.txt
```

```
new
```

### 6.3 限制和对比

```bash
mkdir d
ln d d2
ln g /dev/shm/g
```

```
ln: d: hard link not allowed for directory
ln: failed to create hard link '/dev/shm/g' => 'g': Invalid cross-device link
```

| | 硬链接 | 软链接 |
|---|---|---|
| 本质 | 同一个 inode 的另一个名字 | 一个保存着路径的特殊文件 |
| 删除原文件后 | 仍然可以访问 | 失效 |
| 对目录 | 不允许 | 允许 |
| 跨文件系统 | 不允许（inode 编号只在一个文件系统内有意义） | 允许 |
| 常见用途 | 备份工具节省空间 | 快捷方式、版本切换（如 `/usr/bin/gcc-13 -> aarch64-linux-gnu-gcc-13`） |

实际中**软链接用得多得多**。第 1.1 篇看到的 `/bin -> usr/bin` 就是软链接。

### 6.4 mv 和 cp 的区别

```bash
echo x > f
ls -i f
mv f g
ls -i g
cp g h
ls -i h
```

```
1070112 f
1070112 g
1070114 h
```

在同一个文件系统中，`mv` 只是修改目录项，inode 不变，所以即使移动一个几 GB 的文件也是瞬间完成的；`cp` 创建了一个新的 inode，要复制所有的数据。

## 7. 常见错误

1. **`cp`、`mv` 覆盖了已存在的文件**：它们默认不提示，重要的操作加 `-i`。
2. **`rm -rf` 时路径或变量写错**：没有回收站，无法恢复。
3. **复制目录忘了 `-r`**。
4. **`find -name` 的模式不加引号**：被 Shell 提前展开。
5. **以为删除了原文件，硬链接也会失效**：只要还有一个名字，数据就在。
6. **创建软链接时用了相对路径，却在另一个目录中创建链接**：相对路径是**相对于链接所在的目录**解析的（练习 3）。

## 8. 小结

- `ls -l` 显示类型、权限、链接数、所有者、大小、时间；`-a` 显示隐藏文件，`-h` 方便阅读大小
- `cp -r`、`mv`、`rm -r`、`mkdir -p`；它们默认不提示就覆盖或删除，命令行没有回收站
- `cat`、`less`、`head`、`tail -f` 查看文件；通配符由 Shell 展开；`find` 按名称、类型、大小、时间递归查找
- 文件名只是目录中指向 inode 的条目：硬链接是同一个 inode 的多个名字，软链接是保存路径的文件；实测删除原文件后硬链接仍然可用，软链接失效
- 同一文件系统内 `mv` 不复制数据，inode 不变

## 9. 练习

1. 在主目录中查找所有 7 天内修改过、大于 1 KB 的 `.c` 文件。
2. 一个文件有 3 个硬链接，删除其中两个之后，`ls -l` 中链接数显示多少？文件的数据还在吗？
3. 下面创建的两个软链接，哪个能正常使用？为什么？

   ```bash
   mkdir -p a b
   echo data > a/file.txt
   ln -s a/file.txt b/link1
   ln -s ../a/file.txt b/link2
   ```

<details>
<summary>点击查看答案</summary>

1. `find ~ -name "*.c" -size +1k -mtime -7`。多个条件默认是「并且」的关系。

2. 链接数显示 1，数据还在。只有当最后一个名字也被删除（链接数为 0），并且没有进程打开着这个文件时，数据才会被释放。

3. 只有 `link2` 能用：

   ```
   lrwxrwxrwx 1 lucky lucky 10 Oct 24 01:52 link1 -> a/file.txt
   lrwxrwxrwx 1 lucky lucky 13 Oct 24 01:52 link2 -> ../a/file.txt
   cat: b/link1: No such file or directory
   data
   ```

   软链接中保存的相对路径，是**从链接所在的目录（`b`）出发**解析的。`b/a/file.txt` 不存在，所以 `link1` 是悬空的；`b/../a/file.txt` 就是 `a/file.txt`，所以 `link2` 正常。不确定时可以用绝对路径，或者先 `cd` 到链接所在的目录再创建。

</details>
