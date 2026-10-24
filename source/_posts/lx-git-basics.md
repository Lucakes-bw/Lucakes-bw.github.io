---
title: Git 基础
date: 2026-10-24 10:20:00
updated: 2026-10-24 10:20:00
categories:
  - Linux基础
  - 开发工具
tags:
  - Linux
  - Git
order: 5
description: Git 记录代码的每一次修改，可以随时查看历史、撤销错误、在分支上开发新功能。本文从零开始建一个仓库：工作区、暂存区和提交，.gitignore，撤销修改，分支的快进合并和三方合并，亲手制造并解决一次冲突，最后用一个本地的「服务器」仓库演示 push 被拒绝、pull 之后再 push 的完整过程。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解工作区、暂存区、仓库三个区域；会用 `add`、`commit`、`status`、`diff`、`log`；会写 `.gitignore`；会撤销修改；会创建、合并分支，能解决合并冲突；会使用远程仓库：`clone`、`push`、`pull`。
>
> **前置知识**：会用命令行（第 1 章）。
>
> **实验环境**：Ubuntu 24.04 容器，Git 2.43。为了不依赖网络，第 8 节用一个本地目录充当「服务器」，用法和 GitHub 完全一样。

## 1. 为什么需要 Git

没有版本控制时，常见的做法是复制文件：`calc.c`、`calc_v2.c`、`calc_最终版.c`、`calc_最终版_改.c`……很快就分不清哪个是哪个，也不知道每个版本改了什么。

**Git** 是一个版本控制系统：

- 记录每一次修改：谁、什么时候、改了什么、为什么改；
- 可以查看任意两个版本的差别，可以回到任何一个历史版本；
- 可以开**分支**，在不影响主线的情况下开发新功能；
- 多人协作，各自修改，再合并到一起。

本博客的文章也是用 Git 管理的：每写一批文章，就在一个单独的分支上提交，确认后再合并到 `main` 分支发布。

## 2. 配置和创建仓库

第一次使用前，设置自己的名字和邮箱，它们会记录在每一次提交中：

```bash
git config --global user.name "lucky"
git config --global user.email "lucky@example.com"
git config --global init.defaultBranch main
```

最后一行让新仓库的默认分支叫 `main`。在项目目录中创建仓库：

```bash
mkdir calc && cd calc
git init
```

```
Initialized empty Git repository in /home/lucky/ch2/calc/.git/
```

`git init` 创建了一个隐藏的 `.git` 目录，所有的历史记录都保存在其中。**删除 `.git`，就删除了全部的历史。**

## 3. 第一次提交

写一个程序，编译：

```c
// calc.c
#include <stdio.h>

int main(void)
{
    printf("%d\n", 3 + 4);
    return 0;
}
```

```bash
gcc calc.c -o calc
git status
```

```
On branch main

No commits yet

Untracked files:
  (use "git add <file>..." to include in what will be committed)
	calc
	calc.c

nothing added to commit but untracked files present (use "git add" to track)
```

**`git status` 是最常用的命令**，它告诉你现在的状态和下一步可以做什么。两个文件都是 **untracked**（未跟踪）：Git 还没有管理它们。

```bash
git add calc.c
git status
```

```
On branch main

No commits yet

Changes to be committed:
  (use "git rm --cached <file>..." to unstage)
	new file:   calc.c

Untracked files:
  (use "git add <file>..." to include in what will be committed)
	calc
```

`calc.c` 进入了 **Changes to be committed**（待提交）。提交：

```bash
git commit -m "计算 3 + 4"
git log
```

```
[main (root-commit) 7512acf] 计算 3 + 4
 1 file changed, 7 insertions(+)
 create mode 100644 calc.c
commit 7512acf90e73727b3103f2c9117e73242ebde53d
Author: lucky <lucky@example.com>
Date:   Sat Oct 24 02:15:55 2026 +0000

    计算 3 + 4
```

每个提交有一个 40 位十六进制的编号（**哈希值**），它由提交的内容计算得出。平时用前 7 位就足够区分，比如 `7512acf`。`-m` 后面是**提交信息**，说明这次修改做了什么。

### 3.1 三个区域

为什么要先 `add` 再 `commit`？Git 中的文件处在三个区域：

<figure class="diagram">
<svg viewBox="0 0 680 180" role="img" aria-label="Git 的三个区域：工作区经 git add 进入暂存区，暂存区经 git commit 进入仓库；git restore --staged 用仓库中的版本覆盖暂存区，git restore 用暂存区的内容覆盖工作区">
  <defs>
    <marker id="git-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker>
  </defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="20" y="40" width="170" height="60" rx="5" style="fill:var(--panel-2);stroke:var(--border-2)"/>
    <text x="32" y="64" style="fill:var(--fg)">工作区</text>
    <text x="32" y="86" style="fill:var(--muted);font-size:12px">你正在编辑的文件</text>
    <rect x="255" y="40" width="170" height="60" rx="5" style="fill:var(--code-bg);stroke:var(--warn)"/>
    <text x="267" y="64" style="fill:var(--warn)">暂存区</text>
    <text x="267" y="86" style="fill:var(--muted);font-size:12px">下一次提交的内容</text>
    <rect x="490" y="40" width="170" height="60" rx="5" style="fill:var(--code-bg);stroke:var(--accent)"/>
    <text x="502" y="64" style="fill:var(--accent)">仓库（.git）</text>
    <text x="502" y="86" style="fill:var(--muted);font-size:12px">所有的提交</text>
    <line x1="190" y1="58" x2="253" y2="58" style="stroke:var(--fg-2)" marker-end="url(#git-arrow)"/>
    <text x="196" y="30" style="fill:var(--fg)">git add</text>
    <line x1="425" y1="58" x2="488" y2="58" style="stroke:var(--fg-2)" marker-end="url(#git-arrow)"/>
    <text x="420" y="30" style="fill:var(--fg)">git commit</text>
    <path d="M255,88 C230,130 215,130 190,88" style="fill:none;stroke:var(--fg-2);stroke-dasharray:4 3" marker-end="url(#git-arrow)"/>
    <text x="40" y="150" style="fill:var(--muted);font-size:12px">git restore 文件：丢弃工作区的修改</text>
    <path d="M490,88 C465,130 450,130 425,88" style="fill:none;stroke:var(--warn);stroke-dasharray:4 3" marker-end="url(#git-arrow)"/>
    <text x="330" y="168" style="fill:var(--warn);font-size:12px">git restore --staged 文件：移出暂存区</text>
  </g>
</svg>
</figure>

- **工作区**：磁盘上你看到和编辑的文件；
- **暂存区**（staging area，也叫 index）：准备放进下一次提交的内容。`git add` 把文件的**当前内容**放进暂存区；
- **仓库**：`git commit` 把暂存区中的内容保存为一个新的提交。

暂存区的作用是让你**挑选**要提交的内容：改了 5 个文件，其中 3 个是一个功能，2 个是另一个功能，可以分成两次提交，每次只 `add` 相关的文件。每个提交只做一件事，以后查看历史、撤销修改都会方便得多。

## 4. .gitignore：不要提交编译产物

`git status` 中一直显示着未跟踪的 `calc`。编译出的可执行文件、`.o` 文件可以随时重新生成，不应该提交。创建 `.gitignore` 文件，列出要忽略的文件：

```bash
# 编译产物
*.o
calc
```

```bash
git status --short
```

```
?? .gitignore
```

`--short` 是简短的格式，`??` 表示未跟踪。`calc` 不再出现，只剩下 `.gitignore` 本身，它应该被提交，这样其他人也使用同样的规则：

```bash
git add .gitignore
git commit -m "忽略编译产物"
```

想知道某个文件为什么被忽略：

```bash
git check-ignore -v calc
```

```
.gitignore:3:calc	calc
```

是 `.gitignore` 的第 3 行规则忽略了它。

`.gitignore` 的常用写法：`*.o` 匹配所有 `.o` 文件；`build/` 匹配名为 `build` 的目录；`/calc` 只匹配仓库根目录下的 `calc`；`!important.o` 表示不忽略。

## 5. 查看和撤销修改

把加法改成乘法：

```bash
sed -i "s/3 + 4/3 * 4/" calc.c
git status --short
git diff
```

```
 M calc.c
diff --git a/calc.c b/calc.c
index 9d81ec0..a064ffd 100644
--- a/calc.c
+++ b/calc.c
@@ -2,6 +2,6 @@
 
 int main(void)
 {
-    printf("%d\n", 3 + 4);
+    printf("%d\n", 3 * 4);
     return 0;
 }
```

`git diff` 显示工作区和暂存区的差别：`-` 开头的是删除的行，`+` 开头的是增加的行。`add` 之后再看：

```bash
git add calc.c
git status --short
git diff
git diff --staged
```

```
M  calc.c
diff --git a/calc.c b/calc.c
...
-    printf("%d\n", 3 + 4);
+    printf("%d\n", 3 * 4);
```

`M` 从第二列移到了第一列：第一列是暂存区的状态，第二列是工作区的状态。修改进入暂存区后，`git diff` 什么都不显示了，要用 `git diff --staged` 查看暂存区和最近一次提交的差别。**提交前用 `git diff --staged` 检查一遍**，是个好习惯。

现在反悔了，撤销这次修改：

```bash
git restore --staged calc.c     # 移出暂存区，工作区的修改还在
git status --short
git restore calc.c              # 丢弃工作区的修改
git status --short
grep printf calc.c
```

```
 M calc.c
    printf("%d\n", 3 + 4);
```

`git restore 文件` 用暂存区中的内容覆盖工作区的文件，**没有提交过的修改会永久丢失**，使用前要确认。

已经提交的修改，用 `git revert` 撤销：它生成一个新的提交，内容正好和要撤销的那个提交相反，历史记录保持完整。第 8.2 节会演示。

## 6. 分支

把 `calc.c` 改成用变量计算并提交，现在的历史：

```bash
git log --oneline
```

```
c978b71 用变量 a、b 计算加法
c416ff7 忽略编译产物
7512acf 计算 3 + 4
```

```c
int main(void)
{
    int a = 3, b = 4;
    printf("%d + %d = %d\n", a, b, a + b);
    return 0;
}
```

**分支**是一条独立的开发线。开发新功能时，新建一个分支，在上面随意修改和提交，不影响 `main`；功能完成后，再合并回 `main`。

### 6.1 快进合并

```bash
git switch -c feature-mul        # 创建分支 feature-mul 并切换过去
# 在 a + b 那一行后面加上：printf("%d * %d = %d\n", a, b, a * b);
git commit -am "增加乘法"
git log --oneline
```

```
Switched to a new branch 'feature-mul'
d1fbfeb 增加乘法
c978b71 用变量 a、b 计算加法
c416ff7 忽略编译产物
7512acf 计算 3 + 4
```

（`commit -a` 自动 `add` 所有**已跟踪**的文件中的修改，新文件仍然要手动 `add`。）切换回 `main`：

```bash
git switch main
grep -c printf calc.c
```

```
Switched to branch 'main'
1
```

`main` 上的 `calc.c` 只有一个 `printf`：**切换分支时，Git 会把工作区的文件换成那个分支的内容。**合并：

```bash
git merge feature-mul
```

```
Updating c978b71..d1fbfeb
Fast-forward
 calc.c | 1 +
 1 file changed, 1 insertion(+)
```

**Fast-forward**（快进）：创建分支之后，`main` 上没有新的提交，`feature-mul` 只是在 `main` 的基础上往前走了一步，所以合并时只需要把 `main` 向前移动到 `d1fbfeb`，不需要生成新的提交。合并完成后可以删除分支：`git branch -d feature-mul`。

### 6.2 三方合并

更常见的情况是两边都有新的提交。在分支 `feature-sub` 上增加减法；同时，`main` 上在文件开头加了一行说明：

```bash
git log --oneline --graph --all
```

```
* f000213 增加减法
| * 73b7704 加上文件说明
|/  
* d1fbfeb 增加乘法
* c978b71 用变量 a、b 计算加法
* c416ff7 忽略编译产物
* 7512acf 计算 3 + 4
```

`--graph` 用字符画出分支的结构：两条线从 `d1fbfeb` 分开了。在 `main` 上合并：

```bash
git merge feature-sub -m "合并 feature-sub"
git log --oneline --graph
```

```
Auto-merging calc.c
Merge made by the 'ort' strategy.
 calc.c | 1 +
 1 file changed, 1 insertion(+)
*   7f585b1 合并 feature-sub
|\  
| * f000213 增加减法
* | 73b7704 加上文件说明
|/  
* d1fbfeb 增加乘法
...
```

这次不能快进，Git 做了**三方合并**：以两个分支的共同祖先 `d1fbfeb` 为基准，分别找出两边的修改（一边在开头加了一行，一边在中间加了一行），把它们合在一起，生成一个有**两个父提交**的**合并提交** `7f585b1`。两边改的是不同的地方，Git 自动完成了合并：

```bash
gcc calc.c -o calc && ./calc
```

```
3 + 4 = 7
3 * 4 = 12
3 - 4 = -1
```

## 7. 合并冲突

如果两个分支修改了**同一行**，Git 就不知道该用哪一个了。在分支 `new-values` 上把 `a`、`b` 改成 6 和 2，同时在 `main` 上改成 10 和 5：

```bash
git merge new-values
```

```
Auto-merging calc.c
CONFLICT (content): Merge conflict in calc.c
Automatic merge failed; fix conflicts and then commit the result.
```

```bash
git status
```

```
On branch main
You have unmerged paths.
  (fix conflicts and run "git commit")
  (use "git merge --abort" to abort the merge)

Unmerged paths:
  (use "git add <file>..." to mark resolution)
	both modified:   calc.c
```

**冲突**（conflict）不是错误，只是 Git 需要人来做决定。打开文件：

```c
// 简易计算器
#include <stdio.h>

int main(void)
{
<<<<<<< HEAD
    int a = 10, b = 5;
=======
    int a = 6, b = 2;
>>>>>>> new-values
    printf("%d + %d = %d\n", a, b, a + b);
    printf("%d * %d = %d\n", a, b, a * b);
    printf("%d - %d = %d\n", a, b, a - b);
    return 0;
}
```

- `<<<<<<< HEAD` 到 `=======` 之间是当前分支（`main`）的内容；
- `=======` 到 `>>>>>>> new-values` 之间是要合并进来的分支的内容；
- 冲突之外的部分，Git 已经合并好了。

这时如果直接编译：

```
calc.c: In function 'main':
calc.c:6:1: error: version control conflict marker in file
    6 | <<<<<<< HEAD
      | ^~~~~~~
```

解决冲突，就是**编辑文件，留下正确的内容，删除三行标记**。这里决定保留 `main` 的 10 和 5（也可以保留另一边，或者写成新的内容）：

```c
int main(void)
{
    int a = 10, b = 5;
    printf("%d + %d = %d\n", a, b, a + b);
```

编译运行确认无误，然后用 `git add` 告诉 Git 冲突已经解决，再提交：

```bash
gcc calc.c -o calc && ./calc
git add calc.c
git commit --no-edit
git log --oneline --graph -5
```

```
10 + 5 = 15
10 * 5 = 50
10 - 5 = 5
*   1efb128 Merge branch 'new-values'
|\  
| * 95034ec 换成 6 和 2
* | 9142fb9 换成 10 和 5
|/  
*   7f585b1 合并 feature-sub
|\  
```

`--no-edit` 使用 Git 自动生成的提交信息 `Merge branch 'new-values'`。如果解决到一半想放弃，`git merge --abort` 可以回到合并之前的状态。VS Code 等编辑器会把冲突标记显示成两块不同的颜色，点击按钮就能选择保留哪一边。

**减少冲突的方法**：每个提交小而集中；经常把 `main` 的新修改合并到自己的分支；不要在一个提交中同时修改格式和逻辑（比如重新缩进了整个文件）。

## 8. 远程仓库

到目前为止，所有的操作都在本地。和别人协作，需要一个大家都能访问的**远程仓库**，比如 GitHub、Gitee 上的仓库。为了不依赖网络，这里用一个本地目录充当服务器，`git init --bare` 创建一个只保存历史、没有工作区的仓库，GitHub 上保存的就是这样的仓库：

```bash
git init --bare ~/ch2/remote1/server.git
cd calc
git remote add origin ~/ch2/remote1/server.git
git remote -v
git push -u origin main
```

```
origin	/home/lucky/ch2/remote1/server.git (fetch)
origin	/home/lucky/ch2/remote1/server.git (push)
To /home/lucky/ch2/remote1/server.git
 * [new branch]      main -> main
branch 'main' set up to track 'origin/main'.
```

- `git remote add origin 地址`：添加一个名为 `origin` 的远程仓库。使用 GitHub 时，地址是 `git@github.com:用户名/仓库名.git` 这样的形式；
- `git push -u origin main`：把本地的 `main` 推送到 `origin`，`-u` 记住这个对应关系，以后直接 `git push` 即可。

另一位开发者 bob 把仓库克隆到自己的电脑上：

```bash
git clone ~/ch2/remote1/server.git bob
cd bob && git log --oneline -1
```

```
1efb128 Merge branch 'new-values'
```

`clone` 下载了完整的历史，并自动设置好了 `origin`。

### 8.1 push 被拒绝

现在两个人同时工作：我们增加了除法并推送成功：

```
To /home/lucky/ch2/remote1/server.git
   1efb128..94e6258  main -> main
```

bob 修改了文件说明，也要推送：

```bash
git push
```

```
To /home/lucky/ch2/remote1/server.git
 ! [rejected]        main -> main (fetch first)
error: failed to push some refs to '/home/lucky/ch2/remote1/server.git'
hint: Updates were rejected because the remote contains work that you do not
hint: have locally. This is usually caused by another repository pushing to
hint: the same ref. If you want to integrate the remote changes, use
hint: 'git pull' before pushing again.
```

推送被拒绝了。远程仓库中有 bob 本地没有的提交（我们的除法），如果允许推送，这个提交就会被覆盖。提示告诉 bob：先 `git pull`。

```bash
git pull --no-rebase --no-edit
git log --oneline --graph -4
git push
```

```
From /home/lucky/ch2/remote1/server
   1efb128..94e6258  main       -> origin/main
Auto-merging calc.c
Merge made by the 'ort' strategy.
 calc.c | 1 +
 1 file changed, 1 insertion(+)
*   3131c29 Merge branch 'main' of /home/lucky/ch2/remote1/server
|\  
| * 94e6258 增加除法
* | 83e3773 修改文件说明
|/  
To /home/lucky/ch2/remote1/server.git
   94e6258..3131c29  main -> main
```

`git pull` = `git fetch`（下载远程的新提交）+ `git merge`（合并到当前分支）。两人改的是不同的行，自动合并成功，再推送就成功了。如果改了同一行，就会出现冲突，按第 7 节的方法解决。（`--no-rebase` 指定用合并的方式；另一种方式是 rebase，它把本地的提交「搬」到远程提交的后面，历史是一条直线，初学时先掌握合并即可。）

**千万不要用 `git push --force` 来「解决」被拒绝的推送**，它会用你的历史覆盖远程的历史，别人推送的提交就丢失了。

### 8.2 撤销一个已经推送的提交

决定去掉除法功能。提交已经推送，别人可能已经拉取了，不能修改历史，用 `revert`：

```bash
git revert --no-edit 94e6258
git log --oneline -3
```

```
[main 6e8d6cc] Revert "增加除法"
 Date: Sat Oct 24 02:16:52 2026 +0000
 1 file changed, 1 deletion(-)
6e8d6cc Revert "增加除法"
3131c29 Merge branch 'main' of /home/lucky/ch2/remote1/server
83e3773 修改文件说明
```

`revert` 生成了一个新的提交，删除了除法那一行，「增加除法」这个提交仍然留在历史中。

## 9. 常用命令汇总

| 命令 | 作用 |
|---|---|
| `git init` / `git clone 地址` | 创建仓库 / 克隆远程仓库 |
| `git status` | 查看状态（最常用） |
| `git add 文件` | 放入暂存区 |
| `git commit -m "说明"` | 提交 |
| `git diff` / `git diff --staged` | 工作区 / 暂存区的修改 |
| `git log --oneline --graph` | 查看历史 |
| `git show 提交` | 查看某个提交的内容 |
| `git restore 文件` | 丢弃工作区的修改 |
| `git restore --staged 文件` | 移出暂存区 |
| `git revert 提交` | 用一个新提交撤销某个提交 |
| `git switch -c 分支` / `git switch 分支` | 创建并切换 / 切换分支 |
| `git merge 分支` | 合并分支到当前分支 |
| `git branch` / `git branch -d 分支` | 列出 / 删除分支 |
| `git remote add origin 地址` | 添加远程仓库 |
| `git push` / `git pull` | 推送 / 拉取并合并 |

在很多旧的教程中，切换分支和撤销修改都用 `git checkout`。它的功能太多，容易用错，Git 2.23 起把它拆分成了 `git switch` 和 `git restore`，本文使用新的命令。

## 10. 常见错误

1. **提交了编译产物**：可执行文件、`.o`、`build/` 目录。项目一开始就写好 `.gitignore`。已经提交的，用 `git rm --cached 文件` 从仓库中删除（保留磁盘上的文件），再加入 `.gitignore`。
2. **提交了密码、密钥**：即使在下一个提交中删除，它仍然在历史中，推送到公开仓库就等于公开了。要立即作废这个密钥。
3. **提交信息写成「修改」「update」**：几个月后完全看不出改了什么。写清楚做了什么、为什么。
4. **一个提交包含好几件不相干的修改**：难以查看，也难以单独撤销。
5. **在冲突没有解决完时就提交**：文件中留着 `<<<<<<<` 标记。提交前编译、搜索一下标记。
6. **`git push --force` 覆盖了别人的提交**：先 `git pull`，合并后再推送。
7. **`git restore` 前没有确认**：未提交的修改会永久丢失。

## 11. 小结

- 文件在三个区域之间流动：工作区 →（`add`）→ 暂存区 →（`commit`）→ 仓库；`git status` 随时查看状态
- `.gitignore` 列出不需要提交的文件，比如编译产物
- `git diff` 看未暂存的修改，`git diff --staged` 看已暂存的修改；`git restore` 撤销未提交的修改，`git revert` 撤销已提交的修改
- 分支：`git switch -c` 创建，`git merge` 合并；一边没有新提交时快进合并，两边都有时三方合并，生成合并提交
- 冲突：编辑文件，留下正确内容，删掉标记，`git add` 后提交
- 远程：`clone`、`push`、`pull`；推送被拒绝时先 `pull` 再 `push`，不要 `--force`

## 12. 练习

1. `git status --short` 中的 `MM calc.c` 表示什么？怎样制造出这种状态？
2. 你在 `feature` 分支上工作了一半，代码还不能编译，这时需要切换到 `main` 修一个紧急的 bug。切换时 Git 报错说工作区有修改，怎么办？
3. 下面的 `.gitignore` 中，哪些文件会被忽略？
   ```
   *.o
   build/
   /app
   ```
   文件：`main.o`、`lib/util.o`、`build/main`、`app`、`tools/app`。

<details>
<summary>点击查看答案</summary>

1. 第一列 `M` 表示暂存区中有修改，第二列 `M` 表示工作区中还有**另外的**修改。修改文件后 `git add`，再继续修改同一个文件，就会出现这种状态：

   ```bash
   echo "// x" >> calc.c
   git add calc.c
   echo "// y" >> calc.c
   git status --short
   ```

   ```
   MM calc.c
   ```

   这时 `git diff --staged` 显示已暂存的部分，`git diff` 显示之后的修改；`git commit` 只提交暂存区中的部分。

2. 实测切换时的报错：

   ```
   error: Your local changes to the following files would be overwritten by checkout:
   	calc.c
   Please commit your changes or stash them before you switch branches.
   Aborting
   ```

   报错中已经给出了两种方法：
   - 先提交到 `feature` 分支上（比如 `git commit -am "WIP：未完成"`），切换过去修 bug，回来后继续工作；
   - 用 `git stash` 把未提交的修改暂时收起来，工作区恢复干净，切换分支修 bug；回到 `feature` 后，用 `git stash pop` 取回修改。

   不要用 `git restore` 丢掉修改。

3. 用 `git check-ignore -v` 逐个验证：

   ```
   .gitignore:1:*.o	main.o
   .gitignore:1:*.o	lib/util.o
   .gitignore:2:build/	build/main
   .gitignore:3:/app	app
   ```

   `tools/app` 没有输出，表示不会被忽略。`*.o` 匹配任何目录中的 `.o` 文件；`build/` 匹配名为 `build` 的目录及其中的所有文件；`/app` 开头的 `/` 表示只匹配仓库根目录下的 `app`，所以 `tools/app` 不受影响。

</details>
