---
title: 实战：设计一个博客数据库
date: 2026-10-25 09:50:00
updated: 2026-10-25 09:50:00
categories:
  - 数据库
  - 数据库设计
tags:
  - 数据库
  - MySQL
  - 表设计
order: 2
description: 从需求出发设计一个博客的数据库：用户、文章、分类、标签、多级评论、点赞，共 7 个表。逐一说明每个设计决定，再写出首页列表、评论楼层（递归查询）、标签页、点赞等常用查询。实测中遇到了一个设计缺陷：评论回复评论的自引用外键，让带回复的文章删不掉。最后在 2 万篇文章、50 万条评论上测量「评论数」这个冗余列的价值：热门排行从 27 毫秒降到约 1 毫秒，首页列表却毫无差别。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会从需求中找出实体和关系，设计出符合第三范式的表；会为每一列选择类型和约束，并说清理由；会用递归查询（`WITH RECURSIVE`）读取树形的评论；会处理自引用外键的级联删除；会用测量来决定是否反范式。
>
> **前置知识**：上一篇《范式与表设计》，第 1 章的全部内容。
>
> **实验环境**：MySQL 8.4.13（Docker，2 个 CPU 核），客户端字符集 utf8mb4。

## 1. 需求

一个多人的技术博客：

1. 用户注册时填写登录名、邮箱和密码，登录名和邮箱都不能重复；文章和评论上显示的是昵称；
2. 用户可以写文章，文章有标题、正文，可以先存为草稿，以后再发布；
3. 每篇文章属于一个分类，可以有多个标签；
4. 文章的网址使用可读的短名，例如 `/posts/db-join/`；
5. 用户可以评论文章，也可以回复别人的评论，回复可以一层层嵌套；
6. 用户可以给文章点赞，每人对每篇文章只能点一次；
7. 首页按发布时间列出文章，显示作者、分类、标签和评论数；还有一个按评论数排列的「热门文章」。

## 2. 找出实体和关系

需求中的**名词**通常就是实体（一个表），**名词之间的关系**决定外键和关联表：

| 实体 | 说明 |
|---|---|
| 用户 `users` | `user` 是 MySQL 的关键字，表名用复数避免冲突 |
| 文章 `post` | |
| 分类 `category` | |
| 标签 `tag` | |
| 评论 `comment` | |

| 关系 | 类型 | 怎样建 |
|---|---|---|
| 用户写文章 | 一对多 | `post.author_id` |
| 文章属于分类 | 多对一 | `post.category_id` |
| 文章有标签 | 多对多 | 关联表 `post_tag` |
| 用户评论文章 | 一对多（各一个） | `comment.user_id`、`comment.post_id` |
| 评论回复评论 | 一对多（自己和自己） | `comment.parent_id` 指向 `comment.id` |
| 用户给文章点赞 | 多对多 | 关联表 `post_like` |

「点赞」看起来不像一个名词，但它是用户和文章之间的多对多关系，还带有自己的属性（点赞的时间），所以也是一个表。

<figure class="diagram">
<svg viewBox="0 0 680 300" role="img" aria-label="博客数据库的 7 个表：users、post、category、tag、post_tag、comment、post_like，箭头从外键指向被引用的表">
  <defs>
    <marker id="blog-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/>
    </marker>
  </defs>
  <g style="font-family:var(--mono);font-size:12px">
    <rect x="10" y="110" width="130" height="80" rx="6" style="fill:var(--panel-2);stroke:var(--accent)"/>
    <text x="22" y="130" style="fill:var(--accent)">users</text>
    <text x="22" y="150" style="fill:var(--fg)">id, username</text>
    <text x="22" y="168" style="fill:var(--fg)">email, nickname</text>
    <rect x="260" y="100" width="160" height="100" rx="6" style="fill:var(--panel-2);stroke:var(--accent)"/>
    <text x="272" y="120" style="fill:var(--accent)">post</text>
    <text x="272" y="140" style="fill:var(--warn)">author_id</text>
    <text x="272" y="158" style="fill:var(--warn)">category_id</text>
    <text x="272" y="176" style="fill:var(--fg)">title, slug, status</text>
    <text x="272" y="194" style="fill:var(--fg)">comment_count</text>
    <rect x="530" y="10" width="140" height="56" rx="6" style="fill:var(--panel-2);stroke:var(--accent)"/>
    <text x="542" y="30" style="fill:var(--accent)">category</text>
    <text x="542" y="50" style="fill:var(--fg)">id, name, slug</text>
    <rect x="530" y="120" width="140" height="56" rx="6" style="fill:var(--panel-2);stroke:var(--accent)"/>
    <text x="542" y="140" style="fill:var(--accent)">post_tag</text>
    <text x="542" y="160" style="fill:var(--warn)">post_id, tag_id</text>
    <rect x="530" y="230" width="140" height="56" rx="6" style="fill:var(--panel-2);stroke:var(--accent)"/>
    <text x="542" y="250" style="fill:var(--accent)">tag</text>
    <text x="542" y="270" style="fill:var(--fg)">id, name</text>
    <rect x="260" y="230" width="160" height="62" rx="6" style="fill:var(--panel-2);stroke:var(--accent)"/>
    <text x="272" y="250" style="fill:var(--accent)">comment</text>
    <text x="272" y="268" style="fill:var(--warn)">post_id, user_id</text>
    <text x="272" y="286" style="fill:var(--warn)">parent_id</text>
    <rect x="10" y="10" width="160" height="56" rx="6" style="fill:var(--panel-2);stroke:var(--accent)"/>
    <text x="22" y="30" style="fill:var(--accent)">post_like</text>
    <text x="22" y="50" style="fill:var(--warn)">user_id, post_id</text>
    <line x1="258" y1="140" x2="142" y2="145" style="stroke:var(--fg-2)" marker-end="url(#blog-arrow)"/>
    <line x1="422" y1="155" x2="528" y2="45" style="stroke:var(--fg-2)" marker-end="url(#blog-arrow)"/>
    <line x1="528" y1="150" x2="422" y2="150" style="stroke:var(--fg-2)" marker-end="url(#blog-arrow)"/>
    <line x1="600" y1="178" x2="600" y2="228" style="stroke:var(--fg-2)" marker-end="url(#blog-arrow)"/>
    <line x1="330" y1="228" x2="330" y2="202" style="stroke:var(--fg-2)" marker-end="url(#blog-arrow)"/>
    <line x1="258" y1="270" x2="100" y2="192" style="stroke:var(--fg-2)" marker-end="url(#blog-arrow)"/>
    <path d="M422,285 C470,300 470,240 422,245" style="fill:none;stroke:var(--fg-2)" marker-end="url(#blog-arrow)"/>
    <text x="462" y="270" style="fill:var(--muted)">回复</text>
    <line x1="80" y1="68" x2="80" y2="108" style="stroke:var(--fg-2)" marker-end="url(#blog-arrow)"/>
    <line x1="172" y1="40" x2="300" y2="98" style="stroke:var(--fg-2)" marker-end="url(#blog-arrow)"/>
  </g>
</svg>
</figure>

箭头从外键指向被引用的表；`comment` 指向自己的箭头是「回复」。

## 3. 建表

```sql blog.sql
CREATE DATABASE blog;
USE blog;

CREATE TABLE users (
    id            INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    username      VARCHAR(30)  NOT NULL UNIQUE,      -- 登录名
    email         VARCHAR(100) NOT NULL UNIQUE,
    password_hash CHAR(60)     NOT NULL,             -- bcrypt 的结果，不存明文密码
    nickname      VARCHAR(30)  NOT NULL,             -- 显示的名字，可以重复
    created_at    DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE category (
    id   INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(30) NOT NULL UNIQUE,
    slug VARCHAR(30) NOT NULL UNIQUE                 -- 用在网址中：/category/database/
);

CREATE TABLE post (
    id            INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    author_id     INT UNSIGNED NOT NULL,
    category_id   INT UNSIGNED NOT NULL,
    title         VARCHAR(100) NOT NULL,
    slug          VARCHAR(100) NOT NULL UNIQUE,
    content       MEDIUMTEXT   NOT NULL,              -- 最大 16 MB
    status        ENUM('draft', 'published') NOT NULL DEFAULT 'draft',
    published_at  DATETIME,                           -- 草稿为 NULL
    created_at    DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at    DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    comment_count INT UNSIGNED NOT NULL DEFAULT 0,    -- 冗余：评论数（见第 6 节）
    FOREIGN KEY (author_id) REFERENCES users (id),
    FOREIGN KEY (category_id) REFERENCES category (id)
);

CREATE TABLE tag (
    id   INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(30) NOT NULL UNIQUE
);

CREATE TABLE post_tag (
    post_id INT UNSIGNED NOT NULL,
    tag_id  INT UNSIGNED NOT NULL,
    PRIMARY KEY (post_id, tag_id),
    FOREIGN KEY (post_id) REFERENCES post (id) ON DELETE CASCADE,
    FOREIGN KEY (tag_id) REFERENCES tag (id)
);

CREATE TABLE comment (
    id         INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    post_id    INT UNSIGNED NOT NULL,
    user_id    INT UNSIGNED NOT NULL,
    parent_id  INT UNSIGNED,                          -- 回复哪条评论；直接评论文章时为 NULL
    content    TEXT NOT NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (post_id) REFERENCES post (id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users (id),
    FOREIGN KEY (parent_id) REFERENCES comment (id)
);

CREATE TABLE post_like (
    user_id    INT UNSIGNED NOT NULL,
    post_id    INT UNSIGNED NOT NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (user_id, post_id),                   -- 一个用户对一篇文章只能点一次赞
    FOREIGN KEY (user_id) REFERENCES users (id),
    FOREIGN KEY (post_id) REFERENCES post (id) ON DELETE CASCADE
);
```

逐一说明其中的设计决定：

**用户表。**

- 登录名和邮箱都加了 `UNIQUE`，由数据库保证需求 1 中的「不能重复」。即使程序在注册前检查过，两个人同时注册同一个名字时，检查也可能都通过（第 4 章的并发问题），只有唯一约束是可靠的；
- **不存明文密码。** 存的是密码经过 bcrypt 等专门算法计算出的哈希值，bcrypt 的结果固定是 60 个字符，所以用 `CHAR(60)`。数据库一旦泄露，明文密码会让用户在其他网站上的账号也一起被盗；
- 登录名用于登录，昵称用于显示，两者分开：昵称可以随意修改、可以重复，登录名不行。

**文章表。**

- `slug` 是网址中的短名，要唯一，否则两篇文章会有同一个网址；
- 正文用 `MEDIUMTEXT`（最大 16 MB）。`TEXT` 最大 64 KB，大约 2 万个汉字，长文章可能不够；
- 状态只有两种取值，用 `ENUM`。插入不在列表中的值会被拒绝：

  ```sql
  INSERT INTO post (author_id, category_id, title, slug, content, status)
  VALUES (1, 1, 't', 't', 'x', 'deleted');
  ```

  ```
  ERROR 1265 (01000): Data truncated for column 'status' at row 1
  ```

  `ENUM` 的缺点是增加新的取值要 `ALTER TABLE`。取值会经常变化时，可以改用 `VARCHAR` 加 `CHECK` 约束，或者单独建一个状态表；
- `published_at` 允许为 `NULL`：草稿还没有发布时间。发布时间和创建时间是两回事，文章可以写了一个月才发布，首页要按发布时间排序；
- `updated_at` 的 `ON UPDATE CURRENT_TIMESTAMP` 让 MySQL 在这一行被修改时自动更新它：

  ```sql
  SELECT id, created_at, updated_at FROM post WHERE id = 5;
  UPDATE post SET title = '索引原理' WHERE id = 5;
  SELECT id, created_at, updated_at FROM post WHERE id = 5;
  ```

  ```
  +----+---------------------+---------------------+
  | id | created_at          | updated_at          |
  +----+---------------------+---------------------+
  |  5 | 2026-10-25 10:38:42 | 2026-10-25 10:38:42 |
  +----+---------------------+---------------------+

  +----+---------------------+---------------------+
  | id | created_at          | updated_at          |
  +----+---------------------+---------------------+
  |  5 | 2026-10-25 10:38:42 | 2026-10-25 10:38:58 |
  +----+---------------------+---------------------+
  ```

- `comment_count` 是有意的冗余，第 6 节再讨论。

**评论表。** `parent_id` 指向同一个表的 `id`，这叫**自引用**。直接评论文章的 `parent_id` 为 `NULL`，回复某条评论时填那条评论的 `id`，于是评论组成了一棵棵树。

**外键的删除规则。** 删除文章时，它的标签关联、评论、点赞都没有意义了，所以这几个指向 `post` 的外键都加了 `ON DELETE CASCADE`。指向 `users`、`category`、`tag` 的外键没有级联：删除一个分类，不应该把它下面的文章都删掉，而是应该先把文章移到别的分类，所以保留默认的拒绝删除。

## 4. 示例数据和常用查询

插入 4 个用户、3 个分类、5 个标签、5 篇文章（最后一篇是草稿），以及文章 2 下的一组评论：

```sql
INSERT INTO comment (post_id, user_id, parent_id, content, created_at) VALUES
    (2, 2, NULL, '左连接那一节终于看懂了', '2026-10-25 10:00:00'),
    (2, 1, 1,    '谢谢！', '2026-10-25 10:05:00'),
    (2, 3, 1,    '我也是，之前一直把条件写在 WHERE 里', '2026-10-25 10:20:00'),
    (2, 2, 3,    '哈哈，同款错误', '2026-10-25 10:31:00'),
    (2, 4, NULL, 'NOT IN 那个坑太隐蔽了', '2026-10-25 11:00:00'),
    (2, 1, 5,    '是的，所以 orders.customer_id 要加 NOT NULL', '2026-10-25 11:10:00'),
    (1, 3, NULL, '请问 Windows 上怎么装？', '2026-10-25 12:00:00');
```

用户分别是 lucky、爱丽丝、鲍勃、卡罗尔。完整的数据：

<details>
<summary>blog-data.sql：示例数据</summary>

```sql blog-data.sql
INSERT INTO users (username, email, password_hash, nickname) VALUES
    ('lucky', 'lucky@example.com', '$2b$12$abcdefghijklmnopqrstuuJ9Vb3Qm1lTf1kq0X8bQ0b2fYwJ5mKxa', 'lucky'),
    ('alice', 'alice@example.com', '$2b$12$abcdefghijklmnopqrstuuJ9Vb3Qm1lTf1kq0X8bQ0b2fYwJ5mKxb', '爱丽丝'),
    ('bob',   'bob@example.com',   '$2b$12$abcdefghijklmnopqrstuuJ9Vb3Qm1lTf1kq0X8bQ0b2fYwJ5mKxc', '鲍勃'),
    ('carol', 'carol@example.com', '$2b$12$abcdefghijklmnopqrstuuJ9Vb3Qm1lTf1kq0X8bQ0b2fYwJ5mKxd', '卡罗尔');
INSERT INTO category (name, slug) VALUES ('数据库', 'database'), ('网络', 'network'), ('Linux', 'linux');
INSERT INTO tag (name) VALUES ('MySQL'), ('SQL'), ('TCP'), ('性能'), ('入门');
INSERT INTO post (author_id, category_id, title, slug, content, status, published_at) VALUES
    (1, 1, '数据库与 MySQL 入门', 'db-intro', '……正文……', 'published', '2026-10-25 09:00:00'),
    (1, 1, '多表查询', 'db-join', '……正文……', 'published', '2026-10-25 09:30:00'),
    (1, 2, 'TCP 的常见问题', 'tcp-pitfalls', '……正文……', 'published', '2026-10-24 15:58:00'),
    (2, 3, '我的 Linux 学习笔记', 'alice-linux', '……正文……', 'published', '2026-10-20 21:00:00'),
    (1, 1, '索引原理（写作中）', 'db-index', '……正文……', 'draft', NULL);
INSERT INTO post_tag VALUES (1, 1), (1, 5), (2, 1), (2, 2), (3, 3), (3, 4), (4, 5), (5, 1), (5, 4);
INSERT INTO comment (post_id, user_id, parent_id, content, created_at) VALUES
    (2, 2, NULL, '左连接那一节终于看懂了', '2026-10-25 10:00:00'),
    (2, 1, 1,    '谢谢！', '2026-10-25 10:05:00'),
    (2, 3, 1,    '我也是，之前一直把条件写在 WHERE 里', '2026-10-25 10:20:00'),
    (2, 2, 3,    '哈哈，同款错误', '2026-10-25 10:31:00'),
    (2, 4, NULL, 'NOT IN 那个坑太隐蔽了', '2026-10-25 11:00:00'),
    (2, 1, 5,    '是的，所以 orders.customer_id 要加 NOT NULL', '2026-10-25 11:10:00'),
    (1, 3, NULL, '请问 Windows 上怎么装？', '2026-10-25 12:00:00');
UPDATE post p SET comment_count = (SELECT COUNT(*) FROM comment c WHERE c.post_id = p.id);
INSERT INTO post_like (user_id, post_id) VALUES (2, 2), (3, 2), (4, 2), (2, 1);
```

密码哈希是示例用的假值。最后一条 `UPDATE` 按实际的评论数设置 `comment_count`。

</details>

### 4.1 首页列表

已发布的文章，按发布时间从新到旧，每页 10 篇，显示作者、分类、标签和评论数：

```sql
SELECT p.id, p.title, u.nickname AS 作者, c.name AS 分类,
       (SELECT GROUP_CONCAT(t.name ORDER BY t.name SEPARATOR ', ')
          FROM post_tag pt JOIN tag t ON t.id = pt.tag_id
         WHERE pt.post_id = p.id) AS 标签,
       p.comment_count AS 评论, p.published_at
FROM post p
JOIN users u    ON u.id = p.author_id
JOIN category c ON c.id = p.category_id
WHERE p.status = 'published'
ORDER BY p.published_at DESC
LIMIT 10;
```

```
+----+---------------------+--------+--------+-------------+------+---------------------+
| id | title               | 作者   | 分类   | 标签        | 评论 | published_at        |
+----+---------------------+--------+--------+-------------+------+---------------------+
|  2 | 多表查询            | lucky  | 数据库 | MySQL, SQL  |    6 | 2026-10-25 09:30:00 |
|  1 | 数据库与 MySQL 入门 | lucky  | 数据库 | MySQL, 入门 |    1 | 2026-10-25 09:00:00 |
|  3 | TCP 的常见问题      | lucky  | 网络   | TCP, 性能   |    0 | 2026-10-24 15:58:00 |
|  4 | 我的 Linux 学习笔记 | 爱丽丝 | Linux  | 入门        |    0 | 2026-10-20 21:00:00 |
+----+---------------------+--------+--------+-------------+------+---------------------+
```

草稿没有出现。标签用了 `SELECT` 中的关联子查询，而不是再 `JOIN` 两个表：如果直接连接 `post_tag`，有两个标签的文章就会变成两行（上一章的「放大」），还得再分组。

### 4.2 评论楼层：递归查询

评论是树形的：评论 1 下有回复 2 和 3，回复 3 下又有回复 4。用普通的连接，一个 `JOIN` 只能往下走一层，而回复的层数是不确定的。MySQL 8.0 起支持**递归的公用表表达式**（recursive CTE）：

```sql
WITH RECURSIVE thread AS (
    -- 起点：直接评论文章的评论
    SELECT id, user_id, content, 0 AS depth, CAST(LPAD(id, 6, '0') AS CHAR(200)) AS path
    FROM comment WHERE post_id = 2 AND parent_id IS NULL
    UNION ALL
    -- 递归：找出已找到的评论的回复
    SELECT c.id, c.user_id, c.content, t.depth + 1, CONCAT(t.path, '/', LPAD(c.id, 6, '0'))
    FROM comment c JOIN thread t ON c.parent_id = t.id
)
SELECT CONCAT(REPEAT('    ', t.depth), '#', t.id, ' ', u.nickname, '：', t.content) AS 评论, t.path
FROM thread t JOIN users u ON u.id = t.user_id
ORDER BY t.path;
```

```
#1 爱丽丝：左连接那一节终于看懂了                          000001
    #2 lucky：谢谢！                                        000001/000002
    #3 鲍勃：我也是，之前一直把条件写在 WHERE 里            000001/000003
        #4 爱丽丝：哈哈，同款错误                           000001/000003/000004
#5 卡罗尔：NOT IN 那个坑太隐蔽了                            000005
    #6 lucky：是的，所以 orders.customer_id 要加 NOT NULL   000005/000006
```

`WITH thread AS (…)` 定义了一个临时的结果集 `thread`，后面的查询可以像表一样使用它。`RECURSIVE` 表示它可以引用自己：

1. `UNION ALL` 前面的部分是**起点**，找出两条顶层评论 1 和 5；
2. 后面的部分把**上一轮找到的评论**和 `comment` 表连接，找出它们的回复（2、3、6），再用这一轮的结果找下一层（4），直到某一轮找不到新的行为止。

`depth` 记录层数，用来缩进；`path` 记录从顶层评论到这条评论的编号路径（补齐成 6 位，保证按字符串排序时 `000002` 在 `000010` 之前），按它排序，每条回复就紧跟在它回复的评论之后。

### 4.3 标签页

```sql
-- 标签为 MySQL 的文章
SELECT p.title FROM post p
JOIN post_tag pt ON pt.post_id = p.id
JOIN tag t ON t.id = pt.tag_id
WHERE t.name = 'MySQL' AND p.status = 'published'
ORDER BY p.published_at DESC;

-- 标签云：每个标签有多少篇已发布的文章
SELECT t.name, COUNT(*) AS 文章数 FROM tag t
JOIN post_tag pt ON pt.tag_id = t.id
JOIN post p ON p.id = pt.post_id
WHERE p.status = 'published'
GROUP BY t.id, t.name ORDER BY 文章数 DESC, t.name;
```

```
+---------------------+
| title               |
+---------------------+
| 多表查询            |
| 数据库与 MySQL 入门 |
+---------------------+

+-------+--------+
| name  | 文章数 |
+-------+--------+
| MySQL |      2 |
| 入门  |      2 |
| SQL   |      1 |
| TCP   |      1 |
| 性能  |      1 |
+-------+--------+
```

草稿（文章 5）也打了 MySQL 和性能两个标签，但没有被统计进去。

### 4.4 点赞

爱丽丝已经给文章 2 点过赞，再点一次：

```sql
INSERT INTO post_like (user_id, post_id) VALUES (2, 2);
INSERT IGNORE INTO post_like (user_id, post_id) VALUES (2, 2);
SELECT COUNT(*) AS 点赞数,
       EXISTS (SELECT 1 FROM post_like WHERE user_id = 3 AND post_id = 2) AS 鲍勃已赞
FROM post_like WHERE post_id = 2;
```

```
ERROR 1062 (23000): Duplicate entry '2-2' for key 'post_like.PRIMARY'
Query OK, 0 rows affected, 1 warning (0.00 sec)

+--------+----------+
| 点赞数 | 鲍勃已赞 |
+--------+----------+
|      3 |        1 |
+--------+----------+
```

主键 (user_id, post_id) 保证了「每人只能点一次」。`INSERT IGNORE` 遇到重复时不报错，只产生一个警告，适合「点赞」这种重复操作没有副作用的场景。（`IGNORE` 也会把其他错误降级为警告，例如超出范围的值，使用时要小心。）

## 5. 一个设计缺陷：删不掉的文章

文章 2 写错了，作者要删除它。按照设计，它的评论、标签、点赞都会被级联删除：

```sql
DELETE FROM post WHERE id = 2;
```

```
ERROR 1451 (23000): Cannot delete or update a parent row: a foreign key constraint fails (`blog`.`comment`, CONSTRAINT `comment_ibfk_3` FOREIGN KEY (`parent_id`) REFERENCES `comment` (`id`))
```

删除失败了。文章的删除级联到了它的评论，但评论 1 有回复 2 和 3，它们的 `parent_id` 指向评论 1，而 `parent_id` 这个外键没有指定删除规则，默认拒绝删除被引用的行。只要文章下有一条回复，文章就删不掉。前面几节的查询都正常，这个问题要到真正删除时才暴露。

修正的方法是让回复也随被回复的评论一起删除：

```sql
ALTER TABLE comment
    DROP FOREIGN KEY comment_ibfk_3,
    ADD FOREIGN KEY (parent_id) REFERENCES comment (id) ON DELETE CASCADE;
DELETE FROM post WHERE id = 2;
SELECT COUNT(*) FROM comment;
```

```
Query OK, 1 row affected (0.01 sec)
1
```

文章 2 的 6 条评论（包括多层的回复）、3 个点赞和 2 个标签关联都被删除了，只剩下文章 1 的那条评论。`comment_ibfk_3` 是 MySQL 自动给外键起的名字，可以从 `SHOW CREATE TABLE comment` 中查到。

这个修正对「删除文章」是对的，但对「删除一条评论」未必：用户删除自己的评论，别人的回复也会跟着消失。很多网站的做法是**软删除**：不真正删除这一行，而是标记为已删除，显示成「该评论已删除」，回复照常显示（见练习 3）。

## 6. 评论数：要不要冗余

`post.comment_count` 违反了第三范式：它可以由 `comment` 表统计出来。它值得吗？上一篇说过，要**测量**之后再决定。

先造一批数据：再插入 2 万篇文章和 50 万条评论。评论的分布是不均匀的，少数热门文章的评论很多（递归查询生成数据的方法见练习 2 的答案）：

```sql
SELECT id, title, comment_count FROM post ORDER BY comment_count DESC LIMIT 5;
```

```
+----+--------+---------------+
| id | title  | comment_count |
+----+--------+---------------+
|  6 | 文章 1 |         18408 |
|  7 | 文章 2 |          4785 |
|  8 | 文章 3 |          3357 |
|  9 | 文章 4 |          2671 |
| 10 | 文章 5 |          2258 |
+----+--------+---------------+
```

比较两种场景，每条查询在同一个连接中连续执行 20 次，取平均（减去执行 20 次 `SELECT 1` 的时间），共测 3 轮：

| 查询 | 第 1 轮 | 第 2 轮 | 第 3 轮 |
|---|---|---|---|
| 热门文章，用 `COUNT` 统计 | 27.78 毫秒 | 27.33 毫秒 | 27.76 毫秒 |
| 热门文章，用 `comment_count` | 0.97 毫秒 | 1.17 毫秒 | 1.19 毫秒 |
| 首页 10 篇，用 `COUNT` 子查询 | 1.96 毫秒 | 2.18 毫秒 | 2.18 毫秒 |
| 首页 10 篇，用 `comment_count` | 2.11 毫秒 | 2.28 毫秒 | 2.27 毫秒 |

两条热门文章的查询：

```sql
-- 每次都统计
SELECT p.id, COUNT(c.id) AS n FROM post p LEFT JOIN comment c ON c.post_id = p.id
GROUP BY p.id ORDER BY n DESC LIMIT 10;

-- 读冗余的列
SELECT id, comment_count FROM post ORDER BY comment_count DESC LIMIT 10;
```

结果很有意思：

- **热门文章**：要按评论数**排序**，就必须知道**每一篇**文章的评论数，`COUNT` 的版本每次都要把 50 万条评论全部统计一遍，约 27 毫秒；读 `comment_count` 只要扫描 2 万篇文章，约 1 毫秒，快了二十多倍；
- **首页列表**：只显示 10 篇文章，`COUNT` 子查询只统计这 10 篇的评论。外键自动在 `comment.post_id` 上建了索引，统计一篇文章的评论数只需要读它自己的那些评论，非常快。两种写法都约 2 毫秒，**没有差别**。这 2 毫秒主要花在按发布时间排序 2 万篇文章上，那是第 3 章索引要解决的问题。

所以这个冗余列的价值在于「按评论数排序」，而不是「显示评论数」。如果没有热门文章这个需求，它就是只有成本、没有收益的冗余。

它的成本是什么？每条评论的增加和删除，都必须同时修改 `comment_count`。如果某段代码忘了改，或者两条语句只成功了一条，计数就和实际不一致了。模拟一次「忘记更新计数」：直接插入 3 条评论，不改 `comment_count`。用一条查询找出所有不一致的文章：

```sql
SELECT p.id, p.comment_count, COUNT(c.id) AS actual
FROM post p LEFT JOIN comment c ON c.post_id = p.id
GROUP BY p.id, p.comment_count
HAVING p.comment_count <> actual;
```

```
+-----+---------------+--------+
| id  | comment_count | actual |
+-----+---------------+--------+
| 100 |           296 |    298 |
| 200 |           182 |    183 |
+-----+---------------+--------+
```

保证一致的办法：

- 增加评论和更新计数放在同一个**事务**中，要么都成功，要么都不做（第 4 章）；
- 用 `comment_count = comment_count + 1` 这样的语句增减，而不是在程序中读出来、加 1、再写回去，否则两个人同时评论时会丢失一次更新（1.1 篇文件计数器的问题）；
- 定期运行上面的检查，发现不一致就用实际的统计修复（练习 1）。

## 7. 设计清单

设计完一组表，可以对照检查：

- 每个表都有主键；主键用无业务含义的编号，关联表用两个外键组合；
- 业务上要求唯一的列都有 `UNIQUE`，不要只在程序中检查；
- 不能为空的列都有 `NOT NULL`；可以为空的列，想清楚 `NULL` 表示什么；
- 每个外键都想过删除规则：级联删除、置空，还是拒绝；**自引用的外键尤其要想**；
- 字符串的长度按业务定；字符集 utf8mb4；金额用 `DECIMAL`；可能超过 2038 年的时间用 `DATETIME`；
- 没有用逗号分隔的列表；
- 每个冗余的列都有理由，并且有保证一致的机制；
- 不存明文密码。

## 8. 小结

- 从需求中的名词找实体，从名词之间的关系决定外键和关联表；带属性的多对多关系（点赞的时间）也是一个表
- 唯一约束要由数据库保证；密码只存哈希值；`ENUM` 适合取值固定的状态；`ON UPDATE CURRENT_TIMESTAMP` 自动维护修改时间
- 树形数据（多级评论）用自引用外键存储，用 `WITH RECURSIVE` 查询
- 自引用外键没有级联删除时，带回复的评论（以及它所在的文章）删不掉
- 实测 2 万篇文章、50 万条评论：`comment_count` 让热门排行从约 27 毫秒降到约 1 毫秒，对首页列表没有影响；冗余的价值取决于具体的查询，要测量
- 冗余要用事务和原子的增减语句保证一致，并定期检查

## 9. 练习

1. 写一条 `UPDATE` 语句，修复第 6 节中所有不一致的 `comment_count`。
2. 第 6 节的 2 万篇文章和 50 万条评论是怎样生成的？写出用 `WITH RECURSIVE` 生成 2 万篇文章的语句。
3. 为评论设计软删除：删除后显示「该评论已删除」，回复照常显示。表结构怎样改？评论数应该怎样算？

<details>
<summary>点击查看答案</summary>

1. 先用子查询统计每篇文章实际的评论数，再和 `post` 连接更新（MySQL 支持多表 `UPDATE`）：

   ```sql
   UPDATE post p
   JOIN (SELECT post_id, COUNT(*) AS n FROM comment GROUP BY post_id) x ON x.post_id = p.id
   SET p.comment_count = x.n
   WHERE p.comment_count <> x.n;
   ```

   ```
   Query OK, 2 rows affected (0.05 sec)
   Rows matched: 2  Changed: 2  Warnings: 0
   ```

   修复了 100 和 200 两篇，再执行第 6 节的检查，结果为空。注意这条语句不会处理评论全部被删除的文章（它们不在子查询的结果中，计数应该是 0），完整的修复还要加上 `UPDATE post SET comment_count = 0 WHERE comment_count > 0 AND NOT EXISTS (SELECT 1 FROM comment WHERE post_id = post.id)`。
2. 递归 CTE 可以生成一个从 1 到 n 的序列，再用 `INSERT … SELECT` 插入：

   ```sql
   SET SESSION cte_max_recursion_depth = 1000000;     -- 默认最多递归 1000 层
   INSERT INTO post (author_id, category_id, title, slug, content, status, published_at)
   WITH RECURSIVE seq (n) AS (SELECT 1 UNION ALL SELECT n + 1 FROM seq WHERE n < 20000)
   SELECT 1 + n % 4, 1 + n % 3, CONCAT('文章 ', n), CONCAT('post-', n), REPEAT('正文', 200),
          'published', '2026-01-01' + INTERVAL n * 10 MINUTE
   FROM seq;
   ```

   作者和分类按 `n` 轮流取，发布时间每篇间隔 10 分钟。评论用同样的方法生成 50 万行，文章编号用 `6 + FLOOR(POW(RAND(42 + n), 3) * 19999.999)`：随机数的三次方集中在 0 附近，所以编号小的文章评论特别多，模拟少数热门文章；`RAND` 带上种子，每次生成的数据相同。这两条语句在实验环境中一共用了约 2 秒。
3. 增加一列删除时间，删除时只设置它，不删除这一行：

   ```sql
   ALTER TABLE comment ADD COLUMN deleted_at DATETIME;        -- NULL 表示未删除
   UPDATE comment SET deleted_at = NOW() WHERE id = 1;        -- 「删除」评论 1
   ```

   显示时，`deleted_at` 不为 `NULL` 的评论把内容替换成「该评论已删除」（例如 `IF(c.deleted_at IS NULL, c.content, '该评论已删除')`），回复照常显示，楼层结构不变。评论数要想清楚口径：如果只算未删除的评论，删除时也要把 `comment_count` 减 1，统计的查询要加上 `WHERE deleted_at IS NULL`。软删除的代价是：所有查询都要记得过滤已删除的行，漏掉一处，已删除的内容就又出现了；表中的数据只增不减，需要定期归档或者真正删除很久以前的数据。

</details>
