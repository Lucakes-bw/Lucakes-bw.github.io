---
title: Shell 脚本
date: 2026-10-24 10:15:00
updated: 2026-10-24 10:15:00
categories:
  - Linux基础
  - 开发工具
tags:
  - Linux
  - Shell
  - bash
order: 4
description: 把经常要敲的一串命令写进文件，就是 Shell 脚本。本文讲变量、参数、条件、循环、函数和退出码，重点是最容易出事的地方：变量不加引号时，rm 删掉了两个不相干的文件；变量名拼错时，rm -rf $dir/* 变成了 rm -rf /*。用 set -euo pipefail 和 ShellCheck 防止这些错误，最后写一个完整的备份脚本。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会写和运行 bash 脚本；掌握变量、参数、条件判断、循环、函数；理解退出码在脚本中的作用；**知道为什么变量要加双引号**；会用 `set -euo pipefail` 和 ShellCheck 让脚本更安全；能写一个实用的备份脚本。
>
> **前置知识**：《认识 Linux 与 Shell》、《文本处理与管道》、《进程与系统监控》中的退出码。
>
> **实验环境**：Ubuntu 24.04 容器，bash 5.2.21，ShellCheck 0.9.0。

## 1. 第一个脚本

```bash
#!/bin/bash
# 第一个脚本
name="Linux"
echo "Hello, $name!"
echo "今天是 $(date +%F)，当前目录有 $(ls | wc -l) 个文件"
```

保存为 `hello.sh`，运行：

```bash
./hello.sh
```

```
bash: ./hello.sh: Permission denied
```

新建的文件没有执行权限（《用户、权限与软件包》）。加上执行权限再运行：

```bash
chmod +x hello.sh
./hello.sh
```

```
Hello, Linux!
今天是 2026-10-24，当前目录有 1 个文件
```

- 第一行 `#!/bin/bash` 叫作 **shebang**，告诉内核用哪个程序来解释这个文件。没有它，就由当前的 Shell 来解释，在不同的 Shell 中结果可能不同；
- `#` 开头的是注释；
- 也可以用 `bash hello.sh` 运行，这时不需要执行权限，shebang 也不起作用；
- 为什么要写 `./`？上一章讲过，Shell 只在 `PATH` 中的目录里查找命令，当前目录不在其中。

> 本文写的都是 **bash** 脚本。`#!/bin/sh` 在 Ubuntu 上指向的是 `dash`，它不支持本文中的一些写法（如 `[[ ]]`、数组）。用了 bash 的特性，shebang 就要写 `#!/bin/bash`。

## 2. 变量

```bash
name="Linux"        # 赋值：等号两边不能有空格
echo "$name"        # 使用：前面加 $
echo "${name}er"    # 变量名后面紧跟字母时，用 {} 隔开
```

**等号两边不能有空格**，这是初学者最常犯的错误：

```bash
name = "x"
```

```
bash: name: command not found
```

Shell 把它理解成：运行命令 `name`，参数是 `=` 和 `x`。同样，`echo "$a0"` 是取变量 `a0` 的值（不存在，结果为空），要写成 `"${a}0"`。

常用的写法：

| 写法 | 含义 | 例子（`f=report.tar.gz`） |
|---|---|---|
| `$(命令)` | 命令替换：命令的输出 | `$(date +%F)` → `2026-10-24` |
| `$((表达式))` | 整数运算 | `$((5 * 3 + 1))` → `16` |
| `${var:-默认值}` | 变量为空或不存在时用默认值 | `${x:-默认值}` → `默认值` |
| `${var%后缀}` | 去掉后缀 | `${f%.tar.gz}` → `report` |
| `${var##*.}` | 去掉到最后一个 `.` 为止的前缀 | `${f##*.}` → `gz` |
| `${#var}` | 长度 | `${#f}` → `13` |

Shell 中的变量都是字符串，`$(( ))` 中才当作整数计算，并且只支持整数。

### 2.1 脚本的参数

```bash
#!/bin/bash
echo "脚本名：$0"
echo "参数个数：$#"
echo "第一个参数：$1"
echo '--- "$@" ---'
for a in "$@"; do echo "[$a]"; done
echo '--- $@（不加引号）---'
for a in $@; do echo "[$a]"; done
```

```bash
./args.sh "my report.txt" b
```

```
脚本名：./args.sh
参数个数：2
第一个参数：my report.txt
--- "$@" ---
[my report.txt]
[b]
--- $@（不加引号）---
[my]
[report.txt]
[b]
```

| 变量 | 含义 |
|---|---|
| `$0` | 脚本名 |
| `$1` … `$9`、`${10}` | 第 n 个参数 |
| `$#` | 参数个数 |
| `"$@"` | 所有参数，**每个参数保持独立** |
| `$?` | 上一条命令的退出码 |
| `$$` | 当前 Shell 的 PID |

调用时只传了 2 个参数，但不加引号的 `$@` 把 `my report.txt` 拆成了两个。这就引出了 Shell 脚本中最重要的一条规则。

## 3. 最重要的规则：变量要加双引号

Shell 在展开一个**不加引号**的变量后，还会做两件事：

1. **按空白拆分**（word splitting）：值中有空格，就被拆成多个参数；
2. **通配符展开**（globbing）：值中有 `*`、`?`，就被当作文件名模式展开。

看看它的后果。目录中有 4 个文件，其中一个文件名中有空格：

```bash
touch "my report.txt" my report.txt notes.txt
ls -1
```

```
my
my report.txt
notes.txt
report.txt
```

想删除 `my report.txt`：

```bash
f="my report.txt"
rm $f
ls -1
```

```
my report.txt
notes.txt
```

**要删除的文件还在，另外两个不相干的文件 `my` 和 `report.txt` 却被删除了。**`$f` 不加引号，被拆成了 `my` 和 `report.txt` 两个参数，`rm` 就删除了这两个文件。正确的写法是 `rm "$f"`。

遍历文件时也是同样的问题：

```bash
for f in $(ls); do echo "[$f]"; done
```

```
[my]
[my]
[report.txt]
[notes.txt]
[report.txt]
```

```bash
for f in *; do echo "[$f]"; done
```

```
[my]
[my report.txt]
[notes.txt]
[report.txt]
```

`$(ls)` 的输出被按空白拆分，`my report.txt` 变成了两项；直接用通配符 `*`，每个文件名都保持完整。所以：

- **使用变量时，总是加双引号**：`"$f"`、`"$@"`、`"$(命令)"`；
- **遍历文件用通配符**，不要用 `$(ls)`；
- 只有在确实需要拆分时才不加引号，这种情况很少。

单引号和双引号的区别：双引号中的 `$变量` 和 `$(命令)` 会被展开，单引号中的内容完全原样保留。

## 4. 退出码与条件判断

《进程与系统监控》中讲过，每个命令结束时都返回一个**退出码**：**0 表示成功，非 0 表示失败**。Shell 的条件判断，判断的就是退出码。

### 4.1 if

```bash
if grep -q "/admin" ~/logs/access.log; then
    echo "有人访问了 /admin"
fi
```

```
有人访问了 /admin
```

`if` 后面是一个**命令**，命令的退出码为 0，就执行 `then` 部分。`grep -q` 不输出任何内容，只通过退出码告诉我们是否找到了。

最常用的条件命令是 `[`（也叫 `test`）。`[` 真的是一个命令，所以它和参数之间**必须有空格**：

```bash
if [ -d "$dir" ]; then echo "是目录"; fi
if [ "$a" = "$b" ]; then echo "字符串相等"; fi
if [ "$n" -lt 10 ]; then echo "小于 10"; fi
```

| 条件 | 含义 |
|---|---|
| `-e 文件` / `-f 文件` / `-d 文件` | 存在 / 是普通文件 / 是目录 |
| `-r` / `-w` / `-x 文件` | 可读 / 可写 / 可执行 |
| `-z "$s"` / `-n "$s"` | 字符串为空 / 不为空 |
| `"$a" = "$b"` / `"$a" != "$b"` | 字符串相等 / 不等 |
| `"$a" -eq "$b"`、`-ne`、`-lt`、`-le`、`-gt`、`-ge` | 整数比较（`<` 在 `[ ]` 中是重定向！） |
| `! 条件` | 取反 |

`[ ]` 中的变量也要加引号。变量为空时：

```bash
x=
[ $x = foo ]
```

```
bash: [: =: unary operator expected
```

`$x` 为空，展开后什么都没有剩下，命令变成了 `[ = foo ]`，`[` 看不懂这个表达式。加上引号 `[ "$x" = foo ]`，就是比较空字符串和 `foo`，正常返回 1（不相等）。

bash 还提供了 `[[ ]]`，它不对变量做拆分，支持 `&&`、`||` 和模式匹配（`[[ $f == *.txt ]]`），在 bash 脚本中可以优先使用。

### 4.2 && 和 ||

```bash
mkdir -p out && cd out          # 前一个成功才执行后一个
cd /no/such/dir || exit 1       # 前一个失败才执行后一个
```

`命令 || exit 1` 是脚本中常见的写法：关键的一步失败时，立即退出。

### 4.3 case

按模式匹配，适合处理多个分支：

```bash
case $1 in
    start)        echo "启动" ;;
    stop)         echo "停止" ;;
    *.tar.gz)     echo "压缩包" ;;
    *)            echo "用法：$0 start|stop" ;;
esac
```

## 5. 循环

```bash
for i in {1..3}; do echo -n "$i "; done; echo
```

```
1 2 3 
```

```bash
for f in /etc/host*; do echo "$f"; done
```

```
/etc/host.conf
/etc/hostname
/etc/hosts
```

```bash
n=0
while [ "$n" -lt 3 ]; do
    n=$((n + 1))
done
echo "n=$n"
```

```
n=3
```

**逐行读取**用 `while read`。下面从上一章的访问日志中取出 IP、方法和路径：

```bash
head -3 ~/logs/access.log | while read -r ip _ _ _ _ method path _; do
    echo "$ip -> ${method#\"} $path"
done
```

```
192.168.1.10 -> GET /posts/linked-list/
203.0.113.9 -> GET /wp-login.php
10.0.0.5 -> GET /posts/ds-heap/
```

`read` 把一行按空白拆开，依次赋给后面的变量，最后一个变量得到剩下的全部内容；`_` 是一个约定的名字，表示不需要的字段。日志中的方法是 `"GET`，带着引号，用 `${method#\"}` 去掉开头的引号。`-r` 让 `read` 不把反斜杠当作转义字符，**读取文件时总是加上 `-r`**。

## 6. 函数

```bash
is_even() {
    local n=$1
    (( n % 2 == 0 ))
}

for n in 3 8; do
    if is_even "$n"; then echo "$n 是偶数"; else echo "$n 是奇数"; fi
done
```

```
3 是奇数
8 是偶数
```

- 函数的参数和脚本一样，用 `$1`、`$2`、`$#`、`"$@"` 获取；
- `local` 声明局部变量，否则函数中的变量都是全局的；
- 函数的**返回值就是退出码**：最后一条命令的退出码，或者 `return n` 指定的值。`(( 表达式 ))` 在表达式为真时退出码为 0，所以 `is_even` 可以直接用在 `if` 中；
- 要「返回」一个字符串，就在函数中 `echo` 它，调用时用 `$(函数 参数)` 取得输出。

## 7. 让脚本更安全：set -euo pipefail

默认情况下，脚本中的命令失败了，bash 会**继续执行下一条**。下面三个例子，在开头加上 `set` 的选项前后，结果完全不同。

### 7.1 -e：命令失败时立即退出

```bash
#!/bin/bash
cd /no/such/dir
echo "当前目录：$(pwd)，准备删除这里的所有 .tmp 文件"
```

```
noe.sh: line 2: cd: /no/such/dir: No such file or directory
当前目录：/home/lucky/ch2/sh，准备删除这里的所有 .tmp 文件
exit=0
```

`cd` 失败了，脚本却继续在**原来的目录**中执行。如果下一行是 `rm *.tmp`，删除的就是错误目录中的文件，而退出码 0 还表示一切正常。加上 `set -e`：

```
withe.sh: line 3: cd: /no/such/dir: No such file or directory
exit=1
```

`cd` 失败后脚本立即退出，退出码为 1。

### 7.2 -u：使用未定义的变量时报错

```bash
#!/bin/bash
build_dir="/tmp/build"
echo "将要执行：rm -rf $biuld_dir/*"
```

```
将要执行：rm -rf /*
```

变量名拼错了（`biuld_dir`），bash 把未定义的变量当作空字符串，**`rm -rf $biuld_dir/*` 就变成了 `rm -rf /*`**。（这里只用 `echo` 打印出来，千万不要真的运行。）加上 `set -u`：

```
withu.sh: line 4: biuld_dir: unbound variable
exit=1
```

脚本在使用未定义的变量时报错退出。需要「变量可以不存在」时，用 `${var:-默认值}` 明确地写出来。

### 7.3 pipefail：管道中任何一个命令失败都算失败

```bash
grep ERROR /no/such.log | sort > errors.txt
echo "管道的退出码：$?"
set -o pipefail
grep ERROR /no/such.log | sort > errors.txt
echo "pipefail 后：$?"
```

```
grep: /no/such.log: No such file or directory
管道的退出码：0
grep: /no/such.log: No such file or directory
pipefail 后：2
```

管道的退出码默认是**最后一个命令**的退出码。`grep` 失败了，但 `sort` 成功了，整个管道就算成功，`set -e` 也拦不住。`pipefail` 让管道中任何一个命令失败，整个管道就失败。

所以 bash 脚本的开头通常是：

```bash
#!/bin/bash
set -euo pipefail
```

`set -e` 也有让人意外的地方。下面的脚本在日志中没有 `FATAL` 时，一句话都不输出就退出了：

```bash
#!/bin/bash
set -e
count=$(grep -c "FATAL" ~/logs/access.log)
echo "FATAL 出现了 $count 次"
```

```
exit=1
```

`grep` 没有找到匹配的行时，退出码是 1，`set -e` 把它当作失败，退出了脚本。如果某个命令「失败」是正常情况，就写成 `count=$(grep -c "FATAL" file || true)`。另外，`if`、`while` 的条件中，以及 `&&`、`||` 左边的命令失败，不会触发 `set -e`。

## 8. ShellCheck

**ShellCheck** 是一个检查 Shell 脚本的工具，能发现本文中讲到的几乎所有问题（`sudo apt install shellcheck`）。检查一个有问题的脚本：

```bash
#!/bin/bash
for f in $(ls *.txt); do
    if [ $f = "notes.txt" ]; then
        rm $f
    fi
done
cd $1
```

```bash
shellcheck bad.sh
```

```
In bad.sh line 2:
for f in $(ls *.txt); do
         ^---------^ SC2045 (error): Iterating over ls output is fragile. Use globs.
              ^-- SC2035 (info): Use ./*glob* or -- *glob* so names with dashes won't become options.


In bad.sh line 3:
    if [ $f = "notes.txt" ]; then
         ^-- SC2086 (info): Double quote to prevent globbing and word splitting.

Did you mean: 
    if [ "$f" = "notes.txt" ]; then


In bad.sh line 4:
        rm $f
           ^-- SC2086 (info): Double quote to prevent globbing and word splitting.

Did you mean: 
        rm "$f"


In bad.sh line 7:
cd $1
^---^ SC2164 (warning): Use 'cd ... || exit' or 'cd ... || return' in case cd fails.
   ^-- SC2086 (info): Double quote to prevent globbing and word splitting.

Did you mean: 
cd "$1" || exit
```

每一条都指出了位置、原因和修改建议，`SC2086` 这样的编号可以在 ShellCheck 的网站上查到详细的解释。SC2035 说的是：文件名以 `-` 开头时，比如 `-rf.txt`，会被命令当作选项，用 `./*.txt` 可以避免。VS Code 等编辑器有 ShellCheck 插件，写的时候就能看到提示。

## 9. 综合示例：备份脚本

把一个目录打包备份，文件名带上时间，只保留最近的几个备份：

```bash
#!/bin/bash
# 用法：backup.sh 源目录 备份目录 [保留个数]
set -euo pipefail

log() {
    echo "[$(date +%T)] $*"
}

usage() {
    echo "用法：$0 源目录 备份目录 [保留个数]" >&2
    exit 2
}

[ $# -ge 2 ] || usage
src=$1
dest=$2
keep=${3:-3}                      # 没有第 3 个参数时，默认保留 3 个

if [ ! -d "$src" ]; then
    echo "错误：源目录 $src 不存在" >&2
    exit 1
fi
case $keep in
    ""|*[!0-9]*) echo "错误：保留个数必须是非负整数：$keep" >&2; exit 1 ;;
esac

base=$(basename "$src")
mkdir -p "$dest"
name="$base-$(date +%Y%m%d-%H%M%S).tar.gz"
tar -czf "$dest/$name" -C "$(dirname "$src")" "$base"
log "已备份到 $dest/$name（$(du -h "$dest/$name" | cut -f1)）"

# 文件名中的时间戳让按名字排序就是按时间排序，通配符的结果是按名字排好序的
backups=("$dest/$base"-*.tar.gz)
total=${#backups[@]}
for ((i = 0; i < total - keep; i++)); do
    rm -- "${backups[i]}"
    log "删除旧备份 ${backups[i]}"
done
```

几个要点：

- 错误信息用 `>&2` 输出到**标准错误**，不会混进正常的输出；不同的错误用不同的退出码（参数错误 2，其他错误 1）；
- `case` 中的 `*[!0-9]*` 匹配「含有非数字字符」的字符串，用来检查 `keep` 是不是整数；
- `tar -C 目录` 先切换到源目录的上一级再打包，这样压缩包中的路径是 `logs/...`，而不是完整的绝对路径；
- `backups=(...)` 定义了一个**数组**，`${#backups[@]}` 是元素个数，`${backups[i]}` 是第 i 个元素。通配符的结果按名字排序，而文件名中的时间戳格式是「年月日-时分秒」，按名字排序就是按时间排序，所以前面的就是旧的；
- `rm --`：`--` 表示选项到此结束，后面的参数即使以 `-` 开头也当作文件名。

这个脚本通过了 ShellCheck 的检查。先试几个错误的用法：

```bash
./backup.sh; echo "exit=$?"
./backup.sh ~/nope ~/backups; echo "exit=$?"
./backup.sh ~/logs ~/backups abc; echo "exit=$?"
```

```
用法：./backup.sh 源目录 备份目录 [保留个数]
exit=2
错误：源目录 /home/lucky/nope 不存在
exit=1
错误：保留个数必须是非负整数：abc
exit=1
```

连续备份 5 次（每次间隔 1 秒）：

```bash
for n in 1 2 3 4 5; do ./backup.sh ~/logs ~/backups; sleep 1; done
ls -1 ~/backups
```

```
[02:13:39] 已备份到 /home/lucky/backups/logs-20261024-021339.tar.gz（20K）
[02:13:40] 已备份到 /home/lucky/backups/logs-20261024-021340.tar.gz（20K）
[02:13:41] 已备份到 /home/lucky/backups/logs-20261024-021341.tar.gz（20K）
[02:13:42] 已备份到 /home/lucky/backups/logs-20261024-021342.tar.gz（20K）
[02:13:42] 删除旧备份 /home/lucky/backups/logs-20261024-021339.tar.gz
[02:13:43] 已备份到 /home/lucky/backups/logs-20261024-021343.tar.gz（20K）
[02:13:43] 删除旧备份 /home/lucky/backups/logs-20261024-021340.tar.gz
logs-20261024-021341.tar.gz
logs-20261024-021342.tar.gz
logs-20261024-021343.tar.gz
```

第 4 次备份后开始删除最旧的，始终保留最近的 3 个。路径中有空格也没有问题：

```bash
./backup.sh "/home/lucky/my docs" "/home/lucky/back ups" 2
```

```
[02:13:51] 已备份到 /home/lucky/back ups/my docs-20261024-021351.tar.gz（4.0K）
```

要每天自动运行，可以把它加入 `cron` 定时任务（`crontab -e`，加一行 `0 3 * * * /home/lucky/backup.sh /home/lucky/logs /home/lucky/backups 7`，表示每天 3 点运行，保留 7 个）。

## 10. 常见错误

1. **等号两边加空格**：`name = x` 被当作运行命令 `name`。
2. **变量不加双引号**：值中有空格或通配符时，被拆分或展开，`rm $f` 可能删掉别的文件。
3. **`[` 后面或 `]` 前面没有空格**：`a=1` 时，`[$a = 1]` 报 `[1: command not found`。`[` 是一个命令，`[$a` 被展开成了一个名为 `[1` 的命令。
4. **用 `$(ls)` 遍历文件**：文件名中有空格就出错，用通配符。
5. **不检查 `cd` 的结果**：`cd` 失败后在错误的目录中继续执行。用 `set -e` 或 `cd 目录 || exit`。
6. **在 `[ ]` 中用 `<`、`>` 比较数字**：`<` 是重定向，用 `-lt`、`-gt`，或者用 `(( a < b ))`。
7. **用 `sh script.sh` 运行 bash 脚本**：Ubuntu 上的 `sh` 是 dash，不支持 bash 的特性。用 `bash script.sh` 或 `./script.sh`。
8. **在 Windows 上编辑脚本**：行尾是 `\r\n`，第一行就变成了 `#!/bin/bash\r`，系统去找一个名为 `bash\r` 的程序。实测报 `cannot execute: required file not found`（有些系统上的提示是 `bad interpreter`），这个提示很难让人想到是行尾的问题。用 `dos2unix` 转换，或者在编辑器中把行尾改成 LF。

## 11. 小结

- 脚本第一行写 `#!/bin/bash`，`chmod +x` 后用 `./script.sh` 运行
- 变量：`name=value`（等号两边无空格），`"$name"`，`$(命令)`，`$((运算))`，`${var:-默认值}`；参数：`$1`、`$#`、`"$@"`
- **使用变量时总是加双引号**，遍历文件用通配符
- 条件判断的是**退出码**，0 为真；`[ ]` 是一个命令，内部要有空格；`&&`、`||` 根据退出码决定是否执行
- `for`、`while`、`while read -r` 循环；函数用 `local` 声明局部变量，用退出码返回真假，用 `echo` 返回字符串
- 脚本开头写 `set -euo pipefail`；写完用 ShellCheck 检查

## 12. 练习

1. 写一个脚本 `countc.sh`，统计指定目录（默认是当前目录）下每个 `.c` 文件的行数和总行数。文件名中可能有空格。
2. 下面的脚本有几个问题？
   ```bash
   #!/bin/bash
   dir=$1
   cd $dir
   for f in $(ls *.log); do
       if [ $(wc -l < $f) > 1000 ]; then
           mv $f old/
       fi
   done
   ```

<details>
<summary>点击查看答案</summary>

1. ```bash
   #!/bin/bash
   set -euo pipefail
   dir=${1:-.}
   total=0
   for f in "$dir"/*.c; do
       [ -e "$f" ] || continue              # 一个 .c 文件都没有时，通配符保持原样
       n=$(wc -l < "$f")
       echo "$n $f"
       total=$((total + n))
   done
   echo "合计 $total 行"
   ```

   在一个有 `main.c` 和 `my lib.c` 的目录中测试，再在一个空目录中测试：

   ```
   4 src2/main.c
   2 src2/my lib.c
   合计 6 行
   合计 0 行
   ```

   如果没有匹配的文件，`"$dir"/*.c` 会保持原样，变成字符串 `empty/*.c`，所以要用 `[ -e "$f" ]` 检查文件是否存在。`wc -l < "$f"` 从标准输入读取，输出中只有数字，不带文件名。这个脚本通过了 ShellCheck 的检查。

2. 至少有 5 个问题：
   - 没有 `set -euo pipefail`，也没有检查参数：不传参数时 `$1` 为空，`cd` 回到了主目录，在错误的目录中执行；
   - `cd $dir`、`$f` 都没有加引号，目录名或文件名中有空格就会出错；`cd` 失败了也会继续执行；
   - `for f in $(ls *.log)`：应该写成 `for f in *.log`；
   - **`[ ... > 1000 ]` 中的 `>` 是重定向**：它不会比较大小，而是创建一个名为 `1000` 的文件，`[` 只收到一个非空的参数，所以条件总是成立，所有的 `.log` 文件都会被移走。应该写成 `-gt 1000`；
   - 没有确认 `old/` 目录存在。

   实测：目录中有 5 行的 `small.log` 和 2000 行的 `big.log`，运行后两个文件都被移进了 `old/`，目录中还多了一个文件 `1000`：

   ```
   /tmp/t2:
   1000
   bad2.sh
   old

   /tmp/t2/old:
   big.log
   small.log
   ```

   修改后：

   ```bash
   #!/bin/bash
   set -euo pipefail
   [ $# -eq 1 ] || { echo "用法：$0 目录" >&2; exit 2; }
   cd "$1"
   mkdir -p old
   for f in *.log; do
       [ -e "$f" ] || continue
       if [ "$(wc -l < "$f")" -gt 1000 ]; then
           mv -- "$f" old/
       fi
   done
   ```

   恢复两个文件后再运行，只有 `big.log` 被移走，`small.log` 留在原处；不传参数时输出用法并以退出码 2 退出。

</details>
