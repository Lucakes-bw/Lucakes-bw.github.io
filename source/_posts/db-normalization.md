---
title: 范式与表设计
date: 2026-10-25 09:40:00
updated: 2026-10-25 09:40:00
categories:
  - 数据库
  - 数据库设计
tags:
  - 数据库
  - MySQL
  - 范式
order: 1
description: 表应该怎样拆分？本文把上一章的订单数据合成一张大宽表，实测它的三种异常：只改了李雷的一部分行，他就同时住在北京和上海，按城市统计的金额也跟着错了；没下过单的客户插不进去；删掉张三唯一的订单，张三和降噪耳机就从数据库中消失了。再用逗号分隔的标签列看看违反第一范式的后果：LIKE '%SQL%' 查出了 MySQL 和 NoSQL，把标签 SQL 改名，MySQL 也被改坏了。然后介绍函数依赖和三个范式，一对一、一对多、多对多关系的建表方法，以及什么时候应该有意地违反范式。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解数据冗余带来的更新、插入、删除异常；理解函数依赖，会判断一个表是否满足第一、第二、第三范式，会把不满足的表拆开；会为一对一、一对多、多对多关系建表；知道什么时候、以什么代价违反范式。
>
> **前置知识**：第 1 章的建表、约束和多表查询，示例数据 `shop.sql`（《增删改查》第 1 节）。
>
> **实验环境**：MySQL 8.4.13（Docker），客户端字符集 utf8mb4。

## 1. 一张大宽表

上一章的订单数据分在客户、订单、订单明细、商品四个表中，查询时要连接起来。如果一开始就把它们放在一张表里，查询不就简单了？试试看。把四个表连接的结果存进一张「宽表」：

```sql
CREATE TABLE order_wide (
    order_id      INT UNSIGNED NOT NULL,
    order_time    DATETIME NOT NULL,
    customer_name VARCHAR(50) NOT NULL,
    customer_city VARCHAR(20),
    product_id    INT UNSIGNED NOT NULL,
    product_name  VARCHAR(50) NOT NULL,
    category      VARCHAR(20) NOT NULL,
    quantity      INT UNSIGNED NOT NULL,
    price         DECIMAL(10, 2) NOT NULL,
    PRIMARY KEY (order_id, product_id)
);
INSERT INTO order_wide
SELECT o.id, o.created_at, c.name, c.city, p.id, p.name, p.category, i.quantity, i.price
FROM orders o
JOIN customer c   ON c.id = o.customer_id
JOIN order_item i ON i.order_id = o.id
JOIN product p    ON p.id = i.product_id;
```

`INSERT INTO … SELECT` 把查询的结果直接插入另一个表。一个订单买了几件商品就有几行，主键是 (订单号, 商品号)：

```
+----------+---------------+---------------+--------------+----------+---------+
| order_id | customer_name | customer_city | product_name | quantity | price   |
+----------+---------------+---------------+--------------+----------+---------+
|        1 | 李雷          | 北京          | 机械键盘     |        1 |  299.00 |
|        1 | 李雷          | 北京          | 无线鼠标     |        2 |   89.90 |
|        2 | 李雷          | 北京          | Type-C 线    |        3 |   19.90 |
|        3 | 韩梅梅        | 上海          | 显示器       |        1 | 1299.00 |
|        3 | 韩梅梅        | 上海          | 笔记本支架   |        1 |  129.00 |
|        4 | 张三          | 北京          | 降噪耳机     |        1 | 1499.00 |
|        5 | 韩梅梅        | 上海          | 无线鼠标     |        1 |   89.90 |
|        5 | 韩梅梅        | 上海          | USB 集线器   |        2 |   59.00 |
|        5 | 韩梅梅        | 上海          | Type-C 线    |        5 |   19.90 |
|        6 | 王芳          | 广州          | 机械键盘     |        2 |  279.00 |
+----------+---------------+---------------+--------------+----------+---------+
```

查询确实简单了，但同样的信息被存了很多遍：韩梅梅住在上海，存了 5 遍；李雷住在北京，存了 3 遍。客户的姓名和城市在宽表中一共占 135 字节，在客户表中只要 51 字节。浪费空间还是小事，真正的问题是下面三种**异常**。

### 1.1 更新异常

李雷搬到了上海。改的人只看到了订单 1：

```sql
UPDATE order_wide SET customer_city = '上海' WHERE order_id = 1;
SELECT DISTINCT customer_name, customer_city FROM order_wide WHERE customer_name = '李雷';
```

```
Query OK, 2 rows affected (0.00 sec)
Rows matched: 2  Changed: 2  Warnings: 0

+---------------+---------------+
| customer_name | customer_city |
+---------------+---------------+
| 李雷          | 上海          |
| 李雷          | 北京          |
+---------------+---------------+
```

订单 2 中的李雷还住在北京。现在李雷到底住在哪里？数据库自己也回答不了。错误还会扩散到统计中：

```sql
SELECT customer_city, SUM(quantity * price) AS 金额 FROM order_wide GROUP BY customer_city;
```

```
+---------------+---------+
| customer_city | 金额    |
+---------------+---------+
| 上海          | 2214.20 |
| 北京          | 1558.70 |
| 广州          |  558.00 |
+---------------+---------+
```

李雷的消费一部分算在上海，一部分算在北京，两个数都是错的。同一个事实存了多份，就要保证每一份都同时修改，漏掉一份，数据就互相矛盾了。这叫**更新异常**。

### 1.2 插入异常

赵六刚注册，还没有下单。想把他存进去：

```sql
INSERT INTO order_wide (customer_name, customer_city) VALUES ('赵六', NULL);
```

```
ERROR 1364 (HY000): Field 'order_id' doesn't have a default value
```

宽表的每一行都是「某个订单中的某件商品」，没有订单，就没有地方存客户。同样，一件还没卖出过的新商品也存不进去。这叫**插入异常**。

### 1.3 删除异常

张三退掉了他唯一的订单 4：

```sql
DELETE FROM order_wide WHERE order_id = 4;
SELECT COUNT(*) FROM order_wide WHERE customer_name = '张三';
SELECT COUNT(*) FROM order_wide WHERE product_name = '降噪耳机';
```

```
Query OK, 1 row affected (0.00 sec)
0
0
```

只想删一个订单，张三这个客户、降噪耳机这件商品也一起消失了。这叫**删除异常**。

三种异常的根源是同一个：**一张表描述了好几种东西**（订单、客户、商品），它们的信息纠缠在一起。规范化的思路就是拆开：一种东西一张表，表之间用编号联系，每个事实只存一份。这正是 `shop` 库的设计：李雷的城市只存在 `customer` 表的一行中，改一处就够了；客户和商品各有自己的表，有没有订单都可以存；删除订单也不会影响客户和商品。

## 2. 函数依赖

怎样判断一张表「描述了好几种东西」？要用到**函数依赖**：如果知道了 A 的值，就能唯一确定 B 的值，就说 B 函数依赖于 A，记作 A → B。

在宽表中：

- `order_id → order_time, customer_name, customer_city`：知道订单号，就知道下单时间和客户；
- `product_id → product_name, category`：知道商品号，就知道商品名和分类；
- `(order_id, product_id) → quantity, price`：要同时知道订单号和商品号，才能确定数量和成交价；
- `customer_name → customer_city`：知道客户（假设不重名），就知道他的城市。

函数依赖来自**业务规则**，不是从现有的数据中看出来的。例如现有的数据中没有重名的客户，`customer_name → customer_city` 恰好成立；但业务上允许重名，两个都叫李雷的客户可以住在不同的城市，这个依赖就不成立了。数据中「恰好」存在的对应关系，只要业务上不保证，就不是函数依赖。这也说明了为什么不该用姓名来标识客户（第 5 节）。

**候选键**是能唯一确定一行的最小的一组列；宽表的候选键是 (order_id, product_id)。选作主键的那个候选键就是主键。

## 3. 三个范式

**范式**（normal form）是表设计要满足的一组条件，级别越高，冗余越少。实际中通常做到第三范式。

### 3.1 第一范式：每个值都不可再分

第一范式要求每一列都是**原子的**：一个格子里只放一个值，不放列表，不放可以再拆分的结构。

最常见的违反方式是用逗号分隔的字符串存多个值。例如给文章打标签：

```sql
CREATE TABLE article (id INT PRIMARY KEY, title VARCHAR(50), tags VARCHAR(200));
INSERT INTO article VALUES
    (1, 'MySQL 入门', 'MySQL,数据库'),
    (2, 'Redis 缓存', 'Redis,NoSQL,数据库'),
    (3, 'SQL 优化', 'SQL,MySQL'),
    (4, 'Linux 命令', 'Linux');
```

找出所有标签为 SQL 的文章：

```sql
SELECT id, title, tags FROM article WHERE tags LIKE '%SQL%';
```

```
+----+------------+--------------------+
| id | title      | tags               |
+----+------------+--------------------+
|  1 | MySQL 入门 | MySQL,数据库       |
|  2 | Redis 缓存 | Redis,NoSQL,数据库 |
|  3 | SQL 优化   | SQL,MySQL          |
+----+------------+--------------------+
```

`MySQL` 和 `NoSQL` 中都含有 `SQL`，查出了 3 篇，实际上只有 1 篇。MySQL 提供了 `FIND_IN_SET('SQL', tags)`，它按逗号切分后再比较，能查对；但它无法使用索引（第 3 章），每次都要扫描全表、切分每一行的字符串。

把标签 SQL 改名为「SQL 语言」：

```sql
UPDATE article SET tags = REPLACE(tags, 'SQL', 'SQL 语言');
SELECT id, tags FROM article;
```

```
Rows matched: 4  Changed: 3  Warnings: 0

+----+-------------------------+
| id | tags                    |
+----+-------------------------+
|  1 | MySQL 语言,数据库       |
|  2 | Redis,NoSQL 语言,数据库 |
|  3 | SQL 语言,MySQL 语言     |
|  4 | Linux                   |
+----+-------------------------+
```

只该改 1 篇，却改了 3 篇，`MySQL`、`NoSQL` 都被改坏了。此外，统计每个标签有多少篇文章、保证同一篇文章不重复打同一个标签、限制标签只能从已有的标签中选，用字符串都很难做到。

符合第一范式的做法是把「文章有哪些标签」拆成单独的表，每个（文章，标签）对占一行：

```sql
CREATE TABLE tag (id INT PRIMARY KEY, name VARCHAR(30) NOT NULL UNIQUE);
CREATE TABLE article_tag (
    article_id INT NOT NULL,
    tag_id     INT NOT NULL,
    PRIMARY KEY (article_id, tag_id),
    FOREIGN KEY (article_id) REFERENCES article2 (id),
    FOREIGN KEY (tag_id) REFERENCES tag (id)
);
```

（`article2` 是去掉 `tags` 列的文章表。）同样的几个操作：

```sql
-- 标签为 SQL 的文章
SELECT a.id, a.title FROM article2 a
JOIN article_tag at ON at.article_id = a.id
JOIN tag t ON t.id = at.tag_id
WHERE t.name = 'SQL';

-- 每个标签的文章数
SELECT t.name, COUNT(*) AS 文章数 FROM tag t
JOIN article_tag at ON at.tag_id = t.id
GROUP BY t.id, t.name ORDER BY 文章数 DESC, t.id;

-- 改名
UPDATE tag SET name = 'SQL 语言' WHERE name = 'SQL';

-- 重复打标签
INSERT INTO article_tag VALUES (1, 1);
```

```
+----+----------+
| id | title    |
+----+----------+
|  3 | SQL 优化 |
+----+----------+

+--------+--------+
| name   | 文章数 |
+--------+--------+
| MySQL  |      2 |
| 数据库 |      2 |
| Redis  |      1 |
| NoSQL  |      1 |
| SQL    |      1 |
| Linux  |      1 |
+--------+--------+

Query OK, 1 row affected (0.01 sec)

ERROR 1062 (23000): Duplicate entry '1-1' for key 'article_tag.PRIMARY'
```

查询准确，统计简单，改名只改一行，重复的标签被主键拒绝。需要显示成逗号分隔的形式时，用 `GROUP_CONCAT` 在查询时拼起来：

```sql
SELECT a.title, GROUP_CONCAT(t.name ORDER BY t.id SEPARATOR ',') AS tags
FROM article2 a
JOIN article_tag at ON at.article_id = a.id
JOIN tag t ON t.id = at.tag_id
GROUP BY a.id, a.title ORDER BY a.id;
```

```
+------------+--------------------+
| title      | tags               |
+------------+--------------------+
| MySQL 入门 | MySQL,数据库       |
| Redis 缓存 | 数据库,Redis,NoSQL |
| SQL 优化   | MySQL,SQL 语言     |
| Linux 命令 | Linux              |
+------------+--------------------+
```

改名对所有文章立即生效。**存储时拆开，显示时再拼起来。**

> 「原子」是相对于用途而言的。地址存成一个字符串「北京市海淀区…」，如果从来不需要按省份、城市查询，就可以看作原子的；如果要按城市统计，就应该把城市拆成单独的列。

### 3.2 第二范式：不能只依赖主键的一部分

第二范式要求：满足第一范式，并且每个非主键列都**完全依赖于整个主键**，而不是只依赖于主键的一部分。只有主键由多列组成时，才可能违反第二范式。

宽表的主键是 (order_id, product_id)，但是：

- `product_name`、`category` 只依赖于 `product_id`；
- `order_time`、`customer_name`、`customer_city` 只依赖于 `order_id`。

它们都只依赖于主键的一部分，违反了第二范式。这正是冗余的来源：同一个商品出现在多个订单中，商品名就重复多次；同一个订单有多件商品，下单时间和客户就重复多次。

拆分的方法是：把只依赖于 `product_id` 的列移到以 `product_id` 为主键的表中，只依赖于 `order_id` 的列移到以 `order_id` 为主键的表中，宽表中只留下真正依赖于整个主键的 `quantity` 和 `price`：

```
order_item (order_id, product_id, quantity, price)       主键 (order_id, product_id)
product    (product_id, product_name, category)          主键 product_id
orders     (order_id, order_time, customer_name, customer_city)   主键 order_id
```

### 3.3 第三范式：不能间接依赖于主键

第三范式要求：满足第二范式，并且非主键列之间没有依赖，每个非主键列都**直接**依赖于主键，而不是通过另一个非主键列**传递**依赖。

上一步拆出的 `orders` 表中，`order_id → customer_name`，`customer_name → customer_city`，所以 `customer_city` 是通过 `customer_name` 间接依赖于主键的。一个客户下了几个订单，他的城市就重复几次，1.1 节的更新异常仍然存在。

把客户的信息拆到客户表中，订单表只保留客户的编号：

```
orders   (order_id, order_time, customer_id)
customer (customer_id, customer_name, customer_city)
```

这就得到了 `shop` 库的四个表。顺便用编号代替了姓名作为客户的标识：姓名会重名，也可能修改，不适合当作主键（见第 5 节）。

一句话概括这三个范式：**每个非主键列都依赖于主键，依赖于整个主键，并且只依赖于主键。**

> 还有更高的 BC 范式、第四范式、第五范式，它们处理一些更少见的情况。实际设计中，做到第三范式通常就足够了。

## 4. 表之间的关系

拆分以后，表之间通过外键联系。两种东西之间的关系有三种，建表的方法各不相同。

<figure class="diagram">
<svg viewBox="0 0 680 250" role="img" aria-label="shop 库的四个表及其关系：一个客户有多个订单，一个订单有多条明细，一个商品出现在多条明细中；订单和商品通过订单明细形成多对多的关系">
  <defs>
    <marker id="er-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/>
    </marker>
  </defs>
  <g style="font-family:var(--mono);font-size:12px">
    <rect x="10" y="20" width="150" height="100" rx="6" style="fill:var(--panel-2);stroke:var(--accent)"/>
    <text x="22" y="40" style="fill:var(--accent)">customer</text>
    <text x="22" y="62" style="fill:var(--fg)">id  主键</text>
    <text x="22" y="80" style="fill:var(--fg)">name</text>
    <text x="22" y="98" style="fill:var(--fg)">city</text>
    <rect x="260" y="20" width="160" height="100" rx="6" style="fill:var(--panel-2);stroke:var(--accent)"/>
    <text x="272" y="40" style="fill:var(--accent)">orders</text>
    <text x="272" y="62" style="fill:var(--fg)">id  主键</text>
    <text x="272" y="80" style="fill:var(--warn)">customer_id  外键</text>
    <text x="272" y="98" style="fill:var(--fg)">created_at</text>
    <rect x="260" y="140" width="160" height="100" rx="6" style="fill:var(--panel-2);stroke:var(--accent)"/>
    <text x="272" y="160" style="fill:var(--accent)">order_item</text>
    <text x="272" y="182" style="fill:var(--warn)">order_id    外键</text>
    <text x="272" y="200" style="fill:var(--warn)">product_id  外键</text>
    <text x="272" y="218" style="fill:var(--fg)">quantity, price</text>
    <rect x="520" y="140" width="150" height="100" rx="6" style="fill:var(--panel-2);stroke:var(--accent)"/>
    <text x="532" y="160" style="fill:var(--accent)">product</text>
    <text x="532" y="182" style="fill:var(--fg)">id  主键</text>
    <text x="532" y="200" style="fill:var(--fg)">name, category</text>
    <text x="532" y="218" style="fill:var(--fg)">price, stock</text>
    <line x1="258" y1="76" x2="162" y2="58" style="stroke:var(--fg-2)" marker-end="url(#er-arrow)"/>
    <text x="172" y="54" style="fill:var(--muted)">1</text>
    <text x="238" y="90" style="fill:var(--muted)">N</text>
    <line x1="340" y1="138" x2="340" y2="122" style="stroke:var(--fg-2)" marker-end="url(#er-arrow)"/>
    <text x="348" y="134" style="fill:var(--muted)">N : 1</text>
    <line x1="422" y1="196" x2="518" y2="180" style="stroke:var(--fg-2)" marker-end="url(#er-arrow)"/>
    <text x="430" y="212" style="fill:var(--muted)">N</text>
    <text x="500" y="174" style="fill:var(--muted)">1</text>
  </g>
</svg>
</figure>

箭头从外键指向它引用的主键。

### 4.1 一对多

一个客户有多个订单，一个订单只属于一个客户。在「多」的一方（订单）加一个外键，指向「一」的一方（客户）：`orders.customer_id → customer.id`。这是最常见的关系。

注意外键总是放在「多」的一方。反过来在客户表中存「订单列表」，就回到了第一范式要避免的「一个格子存多个值」。

### 4.2 多对多

一个订单可以有多件商品，一件商品可以出现在多个订单中。多对多不能只用一个外键表示，要增加一个**关联表**：`order_item` 的每一行表示「某订单中有某商品」，它有两个外键，分别指向两边，主键是这两列的组合（同一个订单中同一件商品只占一行）。

关联表还可以存放「这个关系本身」的属性：购买的数量、成交的单价，既不属于订单，也不属于商品，而是属于「这个订单中的这件商品」。上一节的 `article_tag` 也是关联表。

### 4.3 一对一

一个用户有一份详细资料，一份资料只属于一个用户。一对一可以合并成一个表；分开时，通常让副表的主键同时作为外键，指向主表的主键：

```sql
CREATE TABLE user_profile (
    user_id INT UNSIGNED PRIMARY KEY,           -- 既是主键，又是外键
    bio     TEXT,
    avatar  VARCHAR(200),
    FOREIGN KEY (user_id) REFERENCES users (id)
);
```

什么时候拆成两个表？主要是为了性能：把很少用到、又比较大的列（个人简介、设置）移出去，主表更小，常用的查询读的数据更少。也可以用来隔离权限，例如敏感的身份证号放在单独的表中，只授权给少数用户。

## 5. 主键怎样选

- **用无业务含义的编号**（自增整数，或者下一节的其他方案）作为主键，不要用姓名、手机号、身份证号、邮箱。它们可能重复（同名）、可能修改（换手机号），一旦修改，所有引用它的外键都要跟着改；
- 业务上需要唯一的列（邮箱、手机号），另外加 `UNIQUE` 约束；
- 关联表用两个外键的组合作主键，天然防止重复；
- 自增整数简单、紧凑、按顺序插入，是 InnoDB 最友好的主键（3.2 篇会解释原因）。它的缺点是会暴露数据量（订单号连续，竞争对手可以估算你的订单量），在多个数据库之间也不唯一。需要时可以另外生成对外展示的编号，或者使用按时间递增的分布式 ID。

## 6. 反范式：有意的冗余

范式减少了冗余，代价是查询要连接更多的表。有时会**有意地**违反范式，用冗余换取性能或者满足业务需求。但要清楚它是有意的，并且知道由谁、在什么时候保证冗余的数据一致。

**看起来是冗余，其实不是。** `order_item.price` 和 `product.price` 看起来存了两遍，但它们是两个不同的事实：前者是**下单时**的成交价，后者是**现在**的标价。上一章的订单 6 中，机械键盘的成交价是 279，标价是 299。如果订单只引用商品表的价格，商品一调价，所有历史订单的金额就都变了。订单中的收货地址、商品名称也是同样的道理：要记录的是下单那一刻的状态。

**为了性能的冗余。** 文章列表页要显示每篇文章的评论数。严格按范式，每次都要对评论表 `COUNT(*)`；文章多、评论多时，列表页的每一次访问都要统计大量的行。常见的做法是在文章表中增加一个 `comment_count` 列，每增加、删除一条评论，就同时更新它。这样读的时候很快，代价是：

- 写评论时要多写一行；
- 两处修改必须同时成功或同时失败，否则计数和实际的评论数就对不上了，这要用到事务（第 4 章）；
- 很多人同时评论同一篇文章，都要修改同一行，可能产生锁竞争（4.3 篇）。

**汇总表。** 报表中按天、按分类的销售额，如果每次都从订单明细中统计，数据量大时会很慢。可以每天定时把统计结果写入一张汇总表，查询时直接读。代价是数据不是实时的。

一般的原则是：**先按第三范式设计，遇到实际的性能问题、并且测量确认之后，再有针对性地反范式**，而不是一开始就为了「可能的性能问题」到处冗余。

## 7. 常见错误

- **一张大表包罗万象**：更新、插入、删除异常随之而来；
- **用逗号分隔的字符串存多个值**：查询不准、无法索引、无法约束，改用关联表；
- **用姓名、手机号等业务数据作主键**；
- **多对多关系没有关联表**，或者关联表缺少两个外键组合成的主键（同一个关系可以插入多次）；
- **为了「性能」一开始就大量冗余**，却没有保证一致性的机制；
- **把历史快照当成冗余删掉**：订单只引用商品的当前价格，历史订单的金额会随调价而改变。

## 8. 小结

- 冗余带来三种异常：更新异常（只改了一部分）、插入异常（没有依附的行就存不进去）、删除异常（删一行丢了别的信息）。实测只改了李雷的一部分行，他就同时住在两个城市
- 函数依赖 A → B：知道 A 就能唯一确定 B，来自业务规则
- 第一范式：值不可再分，不用逗号分隔的字符串存列表；第二范式：完全依赖于整个主键；第三范式：只直接依赖于主键，没有传递依赖
- 一对多：外键放在「多」的一方；多对多：关联表，两个外键组合成主键；一对一：副表的主键同时是外键
- 主键用无业务含义的编号，业务上唯一的列加 `UNIQUE`
- 反范式是有意的冗余，要有保证一致性的办法；下单时的价格是历史快照，不是冗余

## 9. 练习

1. 一张选课表 `enroll(student_id, course_id, student_name, major, course_name, teacher, teacher_office, score)`，主键是 (student_id, course_id)。业务规则：每门课只有一位老师，每位老师只有一个办公室。写出它的函数依赖，指出违反了哪些范式，并拆分成符合第三范式的表。
2. 用户表中有一列 `phones VARCHAR(100)`，存放用逗号分隔的多个手机号。要求：一个手机号只能被一个用户绑定。用这个设计能用约束保证吗？应该怎样设计？
3. 一个订单只有一个收货地址，但同一个客户可以保存多个常用地址。订单表应该存地址的编号（外键指向地址表），还是把地址的内容复制一份存进订单？

<details>
<summary>点击查看答案</summary>

1. 函数依赖：

   ```
   student_id → student_name, major
   course_id  → course_name, teacher
   teacher    → teacher_office
   (student_id, course_id) → score
   ```

   - 违反第二范式：`student_name`、`major` 只依赖于 `student_id`，`course_name`、`teacher` 只依赖于 `course_id`，都是部分依赖；
   - 违反第三范式：`teacher_office` 通过 `teacher` 传递依赖于 `course_id`（拆出课程表后，它在课程表中仍然是传递依赖）。

   拆分为四个表：

   ```
   student (student_id, student_name, major)
   course  (course_id, course_name, teacher_id)
   teacher (teacher_id, teacher_name, office)
   enroll  (student_id, course_id, score)          主键 (student_id, course_id)
   ```

   `enroll` 是学生和课程之间多对多关系的关联表，成绩是这个关系本身的属性。老师用编号而不是姓名标识。
2. 不能。`UNIQUE` 约束比较的是整个字符串，`'13800000000,13900000000'` 和 `'13900000000'` 是不同的值，同一个手机号可以出现在两个用户的列表中。应该拆出一个表：

   ```sql
   CREATE TABLE user_phone (
       phone   VARCHAR(20) PRIMARY KEY,                -- 一个手机号只能出现一次
       user_id INT UNSIGNED NOT NULL,
       FOREIGN KEY (user_id) REFERENCES users (id)
   );
   ```

   以手机号为主键，就由数据库保证了「一个手机号只属于一个用户」，一个用户可以有多行（多个手机号）。这是一对多关系，外键在「多」的一方。
3. 应该复制一份。地址表中保存的是客户**现在**的常用地址，客户以后可能修改或删除它；订单要记录的是**下单时**送到了哪里。如果订单只存地址的编号，客户改了常用地址，已经发货的历史订单的收货地址也跟着变了；删掉地址，订单还会因为外键而无法删除地址，或者失去收货信息。这和 `order_item.price` 是同样的道理：订单中的这些数据是历史快照，不是冗余。

</details>
