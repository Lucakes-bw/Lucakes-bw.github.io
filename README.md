# lucky 的编程笔记

基于 [Hexo](https://hexo.io) 的学习博客，使用自己编写的终端风格主题 **lucky-term**。专栏：C/C++、数据结构、计算机网络、Linux 基础、数据库、客户端开发（Qt）。

网址：https://lucakes-bw.github.io

## 主题特色

- **终端风格**：命令行式导航（`cd 专栏`、`ls 归档`），文章列表像 `ls -l` 的输出，等宽字体
- **学习路线**：首页按顺序展示各个专栏，每个专栏有章节编号目录
- **阅读进度**：读到文末自动标记已读（也可以手动标记），首页和专栏页显示进度条，首页有「继续阅读」
- **搜索**：按 `/` 或 `Ctrl/⌘ + K` 打开全文搜索，支持多关键词
- **文章页**：代码块显示语言和复制按钮、目录随滚动高亮、顶部阅读进度条、专栏内上一篇 / 下一篇
- **代码跟练**：能独立运行的 C/C++ 示例代码，标题栏有 `▶ run` 和 `edit` 按钮，在浏览器里编译运行（WASM 版 Clang，和刷题页共用）。`edit` 有四种练法：改一改、填空、照着敲、默写，后三种按「输出和原程序一致」判定通过；通过记录显示在代码块、文末、专栏页（`✎ 已练/可练`）和首页学习动态里
- 深色 / 浅色模式（默认跟随系统），手机端适配，Giscus 评论，不蒜子访问统计

> 已读记录保存在读者自己浏览器的 localStorage 中，不会上传，换浏览器或清除数据后会重置。

## 本地运行

```bash
npm install
npx hexo server        # 打开 http://localhost:4000
```

## 写一篇新文章

```bash
npx hexo new post my-article-slug
```

会在 `source/_posts/my-article-slug.md` 生成文件，文件名就是网址：`/posts/my-article-slug/`。按模板填写 front-matter：

| 字段 | 说明 |
| --- | --- |
| `categories` | 两级：第一级是专栏，第二级是章节，必须和 `source/_data/series.yml` 中的名称一致 |
| `order` | 在章节中的排序，决定编号（如 1.2）和上一篇 / 下一篇 |
| `top_group_list` | `true` 时出现在首页侧栏 `★ starred/` 中 |
| `description` | 文章摘要 |

## 目录说明

| 路径 | 作用 |
| --- | --- |
| `_config.yml` | 站点配置（标题、网址、分类英文路径 `category_map`） |
| `_config.lucky-term.yml` | 主题配置（导航、提示符、首页介绍、Giscus、统计） |
| `source/_data/series.yml` | 专栏及章节顺序 |
| `source/_data/essay.yml` | 首页公告和「公告」页 |
| `source/column/` | 专栏总览页和各个专栏的目录页 |
| `themes/lucky-term/layout/` | 页面模板（EJS） |
| `themes/lucky-term/source/css/style.css` | 全部样式，颜色变量在文件开头 |
| `themes/lucky-term/source/js/main.js` | 深浅色、搜索、阅读进度、代码复制、目录、评论 |
| `themes/lucky-term/scripts/series.js` | 专栏数据：章节分组、编号、上一篇 / 下一篇 |
| `.github/workflows/pages.yml` | 推送到 main 后自动发布到 GitHub Pages |

新增章节：在 `series.yml` 中加章节名，同时在 `_config.yml` 的 `category_map` 中加对应的英文路径。

新增专栏：在 `series.yml` 中加一项，在 `source/column/<key>/index.md` 新建目录页（`type: series`、`series: <key>`），并在 `category_map` 中加英文路径。

## 发布

推送到 `main` 分支后，GitHub Actions 会自动构建并发布到 GitHub Pages，大约 1 分钟生效。

> 仓库名必须是 `用户名.github.io`，否则网站会部署在子路径下，需要额外配置 `root`。

## 评论（Giscus）

已开启仓库 Discussions，并在 `_config.lucky-term.yml` 中填好 `repo_id` 和 `category_id`（Announcements 分类）。
还需要安装 [giscus app](https://github.com/apps/giscus) 并授权给 `Lucakes-bw.github.io` 仓库，评论区才能正常加载。

## 访问统计

使用 [不蒜子](https://busuanzi.ibruce.info/)，无需配置。全站访问量显示在页脚和首页侧栏，单篇阅读量显示在文章标题下方。

## 代码跟练

文章里的 C/C++ 代码块满足下面的条件时，会自动出现 `▶ run` 和 `edit` 按钮（规则在 `themes/lucky-term/source/js/judge/snippet-core.js`）：

- 有 `main` 函数，写全了 `#include`
- 只用标准库：不用系统调用、socket、线程、文件、FFmpeg、Qt 等浏览器里没有的东西
- C++ 不用异常（浏览器里的 libc++ 不支持）

自动判断不对时，在代码块前一行加注释：

| 注释 | 作用 |
| --- | --- |
| `<!-- run: off -->` | 不显示运行按钮 |
| `<!-- run: on -->` | 跳过检查，强制显示 |
| `<!-- run: error -->` | 这段代码本来就是演示编译错误 / 崩溃的 |
| `<!-- run: stdin="3 4" -->` | 运行时的标准输入，`\n` 表示换行 |
| `<!-- run: flags="-std=c++23" -->` | 额外的编译参数 |
| `<!-- run: blank="5,8-9" -->` | 「填空」挖掉哪几行（代码块内的行号，从 1 开始），不写就自动挑 |

多个选项写在一起，比如 `<!-- run: stdin="1 2" blank="6" -->`。

练法和判定：

| 练法 | 做法 | 通过条件 |
| --- | --- | --- |
| 改一改 | 原代码随便改着跑 | 不判定 |
| 填空 | 自动挖掉最多 5 行完整语句，行尾注释留作提示；太短的程序（如 hello world）不提供 | 空都填了，输出和原程序一致 |
| 照着敲 | 原代码留在上面，在空编辑器里敲；当前行淡色提示后面的内容，敲错的行标红，注释可以不敲 | 输出和原程序一致 |
| 默写 | 原代码藏起来（可以偷看），可以先看期望输出 | 输出和原程序一致 |

「原程序的输出」是在浏览器里把文章代码也跑一遍得到的，所以永远和文章一致。比较时忽略行尾空白，地址 `0x…` 不比；用到时间、随机数的程序输出不固定，能正常运行就算通过。`run: error` 的代码只能改一改。

写完文章后检查一遍，所有能运行的代码块都会真正编译运行，出错的会列出来；能跟练的代码块还会连跑两次，确认输出稳定（否则判定没法比）：

```bash
npm i --no-save @yowasp/clang@22.0.0-git20542-10   # 和网页里同一个编译器，只需装一次
node tools/check-snippets.mjs                      # 全部文章；也可以跟文章 slug 只查几篇
node tools/check-snippets.mjs --list               # 只统计各专栏有多少代码块能运行
```

