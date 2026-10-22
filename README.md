# lucky 的编程笔记

基于 [Hexo](https://hexo.io) + [AnZhiYu](https://github.com/anzhiyu-c/hexo-theme-anzhiyu) 主题的学习博客，专栏：C/C++、数据结构、计算机网络、Linux 基础。

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
| `order` | 在专栏目录中的排序 |
| `cover` | 封面，可用 `/img/covers/` 下的 `cpp.svg`、`data-structure.svg`、`network.svg`、`linux.svg` |
| `top_group_list` | `true` 时出现在首页「荐」卡片中（最多显示 6 篇） |
| `description` | 首页摘要 |

## 目录说明

| 路径 | 作用 |
| --- | --- |
| `_config.yml` | 站点配置（标题、网址、分类英文路径 `category_map`） |
| `_config.anzhiyu.yml` | 主题配置（菜单、首页卡片、侧边栏、评论、统计） |
| `source/_data/series.yml` | 专栏及章节顺序 |
| `source/_data/essay.yml` | 首页滚动公告和「公告」页 |
| `source/_data/creativity.yml` | 首页左上角滚动的技术图标 |
| `source/column/*/index.md` | 四个专栏目录页 |
| `scripts/series.js` | `{% series key %}` 标签，按章节生成专栏目录 |
| `scripts/catalog-list.js` | 首页分类栏只显示一级分类 |
| `source/css/custom.css` | 自定义样式 |
| `.github/workflows/pages.yml` | 推送到 main 后自动发布到 GitHub Pages |

新增章节：在 `series.yml` 中加章节名，同时在 `_config.yml` 的 `category_map` 中加对应的英文路径。

## 发布到 GitHub Pages

1. 在 GitHub 新建公开仓库 `lucky.github.io`
2. 推送代码：
   ```bash
   git add -A && git commit -m "init blog"
   git branch -M main
   git remote add origin https://github.com/lucky/lucky.github.io.git
   git push -u origin main
   ```
3. 仓库 **Settings → Pages → Build and deployment → Source** 选择 **GitHub Actions**
4. 等 Actions 跑完，访问 https://lucky.github.io

## 开启评论（Giscus）

1. 仓库 **Settings → General → Features** 勾选 **Discussions**
2. 安装 [giscus app](https://github.com/apps/giscus) 并授权给该仓库
3. 打开 https://giscus.app ，填写仓库 `lucky/lucky.github.io`，Discussion 分类选 **Announcements**
4. 把页面生成的 `data-repo-id` 和 `data-category-id` 填到 `_config.anzhiyu.yml` 的 `giscus.repo_id` 和 `giscus.category_id`

## 访问统计

使用 [不蒜子](https://busuanzi.ibruce.info/)，无需配置。全站访问量显示在侧边栏，单篇阅读量显示在文章标题下方。
