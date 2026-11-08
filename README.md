# lucky 的编程笔记

一个从 C 语言一路学到网络编程、数据库、音视频和分布式系统的学习博客。基于 [Hexo](https://hexo.io)，主题 **lucky-term** 是自己写的终端风格主题。

🌐 **网址**：https://lucakes-bw.github.io

文章里的代码能直接在浏览器里运行、改写和跟练；配套的实验程序都放在仓库里，文中的每一个输出都来自真实运行的结果。

## 专栏

| 专栏 | 内容 | 文章 |
| --- | --- | --- |
| C/C++ | 从 C 语法到现代 C++（C++11/14/17/20），再到 C++ 工程实践 | 70 |
| 设计模式 | 用 C++ 讲 GoF 设计模式和 C++ 惯用法，配类图和逐步演示（编写中） | 10 |
| 数据结构 | 用 C/C++ 实现常见数据结构与算法，含算法设计和「地铁换乘查询」实战项目 | 38 |
| 计算机网络 | 分层模型、TCP/IP、Socket 编程、服务器开发（网络库 tinynet）、网络排障 | 30 |
| Linux 基础 | 常用命令、工具链、文件 IO、进程线程、内存管理、性能分析、容器原理 | 36 |
| 数据库 | 以 MySQL 为例讲 SQL、索引、事务与锁、复制调优；Redis 缓存；用 C 写 LSM 树存储引擎 | 34 |
| 客户端开发 | Qt 6 + C++ 桌面客户端，最后做一个接上网络专栏服务器的聊天程序 | 20 |
| 音视频开发 | 手写码流和封装格式解析器，再用 FFmpeg 写解码、编码、播放器和推流 | 30 |
| 分布式系统 | 用 C 写分布式键值存储 dkv，在容器里制造故障，再学习 Raft 等算法 | 6 |

此外还有一个 **刷题** 栏目：102 道题（含设计模式专栏的「重构挑战」题），支持 C++ / Python / JavaScript，在浏览器里判题。

## 功能

**阅读**

- 终端风格：命令行式导航（`cd 专栏`、`vim 刷题`、`ls 归档`），文章列表像 `ls -l` 的输出
- 学习路线：首页按顺序展示各个专栏，每个专栏有带章节编号的目录
- 阅读进度：读到文末自动标记已读，首页和专栏页显示进度条，首页有「继续阅读」
- 文章页：代码块复制按钮、目录随滚动高亮、顶部进度条、专栏内上一篇 / 下一篇
- 术语卡片：正文里第一次出现的专业术语（RAII、TIME_WAIT、回表、PTS…）带虚线下划线，悬停或点一下显示一句解释，并链接到详细讲解它的文章。术语表在 `source/_data/glossary.yml`
- 搜索：按 `/` 或 `Ctrl/⌘ + K` 打开全文搜索，支持多关键词
- 外观：终端 / iOS 两种界面风格，17 套配色（默认跟随系统深浅色），手机端适配
- Giscus 评论，不蒜子访问统计，Atom 订阅（`/atom.xml`）和 sitemap

**动手**

- **代码跟练**：能独立运行的 C/C++ 示例代码块带 `▶ run` 和 `edit` 按钮，用 WASM 版 Clang 在浏览器里编译运行。四种练法：改一改、填空、照着敲、默写，练习记录显示在代码块、文末和首页学习动态
- **刷题**：题库带在线编辑器和判题，每道题关联到对应的专栏文章；「重构挑战」题给出结构很差的起始代码，提交时还会检查代码结构
- **错题本与复习**（`/review/`）：通过的题目和跟练过的代码块按 1、2、4、7、15、30 天的间隔提醒复习，没通过的题收进错题本；首页「学习动态」旁显示今天要复习几项
- **数据结构演示**：文章中可以嵌入逐步执行的动画演示，当前代码行同步高亮；网络专栏的演示中，数据包会沿着连线飞过去
- **UML 类图和时序图**：正文写一个 ```` ```uml ```` 代码块，内容是 YAML（类图写 `classes` / `rels`，时序图写 `participants` / `messages`），构建时换成内联 SVG，深浅色自动适配。字段说明在 `themes/lucky-term/source/js/uml.js` 开头，例子见 `source/_posts/dp-uml-class-diagram.md`。演示（dsviz）里也能画类图：场景写 `kind: 'uml'`，front-matter 加 `uml_viz: true`，参考 `source/js/viz/dp-uml.js`。

**逐字节拆包**：网络专栏中嵌入真实抓到的数据包，浏览器里逐层解析（以太网、IPv4、TCP/UDP、DNS、HTTP、TLS），十六进制和字段双向高亮，读者还能粘贴自己用 tcpdump 抓的包

> 已读和练习记录都保存在读者自己浏览器的 localStorage 中，不会上传；换浏览器或清除数据后会重置。
> 页脚的「学习记录备份」（首页学习记录下的「备份 / 恢复」）可以把记录和草稿导出成 JSON 文件，在别的浏览器里导入。导入是合并：已读取并集，做题状态保留更好的一条，本机已有的草稿不会被覆盖（规则见 `themes/lucky-term/source/js/backup.js`）。

## 本地运行

需要 Node.js 22。

```bash
npm install
npx hexo server        # 打开 http://localhost:4000
```

推送到 `main` 分支后，GitHub Actions（`.github/workflows/pages.yml`）会自动构建并发布到 GitHub Pages，大约 1 分钟生效。

## 目录结构

```
├── _config.yml                 站点配置（标题、网址、分类英文路径 category_map）
├── _config.lucky-term.yml      主题配置（导航、首页、配色、Giscus、统计）
├── source/
│   ├── _posts/                 文章
│   ├── _problems/              题库（题目 + solutions/ 参考解答，不发布）
│   ├── _data/series.yml        专栏及章节顺序
│   ├── _data/essay.yml         首页公告和「公告」页
│   ├── _data/glossary.yml      术语表（文章里的术语卡片）
│   ├── column/                 专栏总览页和各专栏目录页
│   ├── js/viz/                 数据结构演示脚本
│   ├── downloads/              实战项目的起始代码压缩包
│   └── media/                  音视频专栏的测试视频
├── themes/lucky-term/
│   ├── layout/                 页面模板（EJS）
│   ├── scripts/                生成逻辑：专栏编号、搜索、题库、代码跟练、订阅
│   └── source/
│       ├── css/style.css       全部样式，配色变量在文件开头
│       └── js/                 main.js（前端交互）、dsviz.js（演示引擎）、judge/（编译器、判题、跟练）
├── projects/                   各章配套的实验程序和实战项目
└── tools/                      检查脚本：代码块能否运行、题目能否通过
```

## 写文章

```bash
npx hexo new post my-article-slug
```

生成 `source/_posts/my-article-slug.md`，网址是 `/posts/my-article-slug/`。front-matter：

| 字段 | 说明 |
| --- | --- |
| `categories` | 两级：专栏 / 章节，名称必须和 `source/_data/series.yml` 一致 |
| `order` | 章节内的排序，决定编号（如 1.2）和上一篇 / 下一篇 |
| `description` | 文章摘要 |
| `top_group_list` | `true` 时出现在首页侧栏 `★ starred/` |
| `viz` | 用到的演示脚本（`source/js/viz/*.js`），正文中写 `<div class="dsviz" data-viz="名字"></div>` |

**逐字节拆包**：front-matter 的 `viz` 中加上 `net-pkts`，正文写 `<div class="pktview" data-pkt="名字"></div>`（加 `data-paste` 可以让读者粘贴自己的包）。包来自 `projects/net-pkts/` 下的真实抓包，在 `export.py` 中挑选后运行 `python3 projects/net-pkts/export.py` 生成 `source/js/viz/net-pkts.js`。

- **新增章节**：在 `series.yml` 里加章节名，并在 `_config.yml` 的 `category_map` 中加英文路径
- **新增专栏**：在 `series.yml` 里加一项，新建 `source/column/<key>/index.md`（`type: series`、`series: <key>`），并在 `category_map` 中加英文路径

### 代码跟练

C/C++ 代码块满足以下条件时自动出现运行按钮（规则见 `themes/lucky-term/source/js/judge/snippet-core.js`）：

- 有 `main` 函数，`#include` 写全
- 只用标准库：不用系统调用、socket、线程、文件、FFmpeg、Qt 等浏览器里没有的东西
- C++ 不用异常（浏览器里的 libc++ 不支持）

自动判断不对时，在代码块前一行加注释，多个选项可以写在一起，如 `<!-- run: stdin="1 2" blank="6" -->`：

| 注释 | 作用 |
| --- | --- |
| `<!-- run: off -->` | 不显示运行按钮 |
| `<!-- run: on -->` | 跳过检查，强制显示 |
| `<!-- run: error -->` | 本来就是演示编译错误 / 崩溃的代码（只能改一改） |
| `<!-- run: stdin="3 4" -->` | 标准输入，`\n` 表示换行 |
| `<!-- run: flags="-std=c++23" -->` | 额外的编译参数 |
| `<!-- run: blank="5,8-9" -->` | 「填空」挖掉的行（代码块内行号，从 1 开始），不写则自动挑 |

| 练法 | 做法 | 通过条件 |
| --- | --- | --- |
| 改一改 | 随便改着跑 | 不判定 |
| 填空 | 挖掉最多 5 行完整语句，行尾注释留作提示；太短的程序不提供 | 空都填了且输出一致 |
| 照着敲 | 直接在变灰的原代码上敲，敲过的恢复高亮；敲错不前进（标红），缩进、注释、中文、对齐用的多余空格自动跳过；进度存在本机 | 敲完即通过，自动运行一次 |
| 默写 | 原代码藏起来（可以偷看），可以先看期望输出 | 输出一致 |

「期望输出」是在浏览器里把原代码跑一遍得到的，所以总和文章一致。比较时忽略行尾空白和 `0x…` 地址；输出依赖时间、随机数的程序能正常运行就算通过。

### 出题

每道题是 `source/_problems/<slug>.md`，front-matter 描述函数签名、示例和测试用例，参考解答放在 `source/_problems/solutions/<slug>.{cpp,py,js}`。完整格式见 [source/_problems/README.md](source/_problems/README.md)。

## 检查

写完文章或题目后跑一遍。脚本用的是和网页里同一个编译器，需要先装一次（不写进 `package.json`，免得部署时也下载）：

```bash
npm i --no-save @yowasp/clang@22.0.0-git20542-10
```

```bash
node tools/check-snippets.mjs           # 编译运行所有能跑的代码块；能跟练的连跑两次确认输出稳定
node tools/check-snippets.mjs --list    # 只统计各专栏有多少代码块能运行
node tools/check-problems.mjs           # 检查题目格式、C++ 模板能编译、三种语言的参考解答全部通过
```

两个脚本后面都可以跟 slug，只检查指定的几篇 / 几题。

## 实验程序

`projects/` 下是各章配套的代码，每个目录都有自己的 README，写明编译方法和文中输出的运行环境：

| 目录 | 对应章节 |
| --- | --- |
| `cpp-eng/wordfreq` | C/C++ · C++ 工程实践 |
| `dp-lab` | 设计模式（每篇一个目录） |
| `algo`、`ds-extra` | 数据结构 · 算法设计、补充篇 |
| `ds-metro` | 数据结构 · 实战项目「地铁换乘查询」（起始代码 + 各里程碑参考实现） |
| `net-server/tinynet` | 计算机网络 · 服务器开发 |
| `net-diag` | 计算机网络 · 网络排障 |
| `net-pkts` | 计算机网络 · 文章中逐字节拆开的真实抓包（DNS、HTTP、TLS） |
| `lx-memory`、`lx-perf`、`lx-container` | Linux 基础 · 内存管理、性能分析、容器原理 |
| `redis-lab`、`lsm-kv` | 数据库 · Redis、存储引擎 |
| `qt-chat` | 客户端开发 · 综合实战 QtChat |
| `dkv` | 分布式系统 |

带 `pack.sh` 的项目修改后需要重新打包到 `source/downloads/`。

## 部署备注

- 仓库名必须是 `用户名.github.io`，否则网站会部署在子路径下，需要额外配置 `root`
- **Giscus 评论**：仓库已开启 Discussions，`_config.lucky-term.yml` 中填好了 `repo_id` 和 `category_id`（Announcements 分类）；还需要安装 [giscus app](https://github.com/apps/giscus) 并授权给本仓库，评论区才能加载
- **访问统计**：使用 [不蒜子](https://busuanzi.ibruce.info/)，无需配置。全站访问量在页脚和首页侧栏，单篇阅读量在文章标题下方
