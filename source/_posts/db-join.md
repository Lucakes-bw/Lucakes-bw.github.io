---
title: 多表查询
date: 2026-10-25 09:30:00
updated: 2026-10-25 09:30:00
categories:
  - 数据库
  - SQL基础
tags:
  - 数据库
  - MySQL
  - SQL
order: 4
description: 数据分散在客户、订单、订单明细、商品四个表中，查询时要把它们连接起来。本文配有内连接、左连接逐行配对的演示，并实测几个常见的坑：把右表的条件写进 WHERE，左连接悄悄变成了内连接；连接之后再统计，韩梅梅的 2 个订单被算成了 5 个；NOT IN 的子查询中混进一个 NULL，结果就变成了空集。最后介绍子查询、EXISTS 和 UNION。
top_group_list: true
disableNunjucks: true
viz: [db-join]
---

> **本文目标**：理解为什么数据要分在多个表中；会用内连接、左连接查询多个表，理解连接条件写在 `ON` 和 `WHERE` 中的区别；会在连接后正确地分组统计；会使用标量子查询、`IN`、`EXISTS`、派生表和关联子查询；知道 `NOT IN` 遇到 `NULL` 的问题；会用 `UNION` 合并结果。
>
> **前置知识**：上一篇《增删改查》，以及它的示例数据 `shop.sql`。
>
> **实验环境**：MySQL 8.4.13（Docker），客户端字符集 utf8mb4。所有查询都在执行 `shop.sql` 后的原始数据上运行。

## 1. 为什么要分成多个表

一个订单有下单的客户、时间，和若干件商品。如果把这些都放在一个表中：

| 订单 | 客户 | 客户城市 | 下单时间 | 商品 | 分类 | 数量 | 单价 |
|---|---|---|---|---|---|---|---|
| 1 | 李雷 | 北京 | 10-01 09:12 | 机械键盘 | 外设 | 1 | 299.00 |
| 1 | 李雷 | 北京 | 10-01 09:12 | 无线鼠标 | 外设 | 2 | 89.90 |
| 2 | 李雷 | 北京 | 10-08 20:45 | Type-C 线 | 配件 | 3 | 19.90 |

李雷的城市重复存了三次，订单 1 的时间重复存了两次。重复带来的问题是：李雷搬到上海，要改三行，漏改一行数据就互相矛盾了；一个还没有下过单的客户，根本无处存放。

所以 `shop` 库把它们拆成了四个表，每个表只描述一种东西，表之间用编号联系：

```
customer (id, name, city, created_at)                   客户
orders (id, customer_id, created_at)                    订单：属于哪个客户
order_item (order_id, product_id, quantity, price)      订单明细：哪个订单买了哪个商品
product (id, name, category, price, stock)              商品
```

怎样拆分是第 2 章的内容。拆开存放，查询时就要**连接**（join）起来。本文主要用到客户表和订单表：

```
+----+--------+------+------------+     +----+-------------+---------------------+
| id | name   | city | created_at |     | id | customer_id | created_at          |
+----+--------+------+------------+     +----+-------------+---------------------+
|  1 | 李雷   | 北京 | 2026-01-05 |     |  1 |           1 | 2026-10-01 09:12:00 |
|  2 | 韩梅梅 | 上海 | 2026-02-11 |     |  2 |           1 | 2026-10-08 20:45:00 |
|  3 | 张三   | 北京 | 2026-03-20 |     |  3 |           2 | 2026-10-03 13:30:00 |
|  4 | 王芳   | 广州 | 2026-05-02 |     |  4 |           3 | 2026-10-10 11:05:00 |
|  5 | 赵六   | NULL | 2026-09-30 |     |  5 |           2 | 2026-10-15 22:10:00 |
+----+--------+------+------------+     |  6 |           4 | 2026-10-20 08:00:00 |
                                        +----+-------------+---------------------+
```

李雷和韩梅梅各有 2 个订单，张三、王芳各 1 个，赵六没有下过订单。

## 2. 笛卡尔积

`FROM` 后面写两个表，不加任何条件：

```sql
SELECT COUNT(*) FROM customer, orders;
SELECT c.id, c.name, o.id AS order_id, o.customer_id
FROM customer c CROSS JOIN orders o ORDER BY c.id, o.id LIMIT 8;
```

```
+----------+
| COUNT(*) |
+----------+
|       30 |
+----------+

+----+--------+----------+-------------+
| id | name   | order_id | customer_id |
+----+--------+----------+-------------+
|  1 | 李雷   |        1 |           1 |
|  1 | 李雷   |        2 |           1 |
|  1 | 李雷   |        3 |           2 |
|  1 | 李雷   |        4 |           3 |
|  1 | 李雷   |        5 |           2 |
|  1 | 李雷   |        6 |           4 |
|  2 | 韩梅梅 |        1 |           1 |
|  2 | 韩梅梅 |        2 |           1 |
+----+--------+----------+-------------+
```

5 个客户 × 6 个订单 = 30 行：每个客户和每个订单都配了一次，这叫**笛卡尔积**（`CROSS JOIN`）。`customer c` 中的 `c` 是表的别名，之后用 `c.id`、`o.id` 区分两个表中同名的列。

其中只有 `c.id = o.customer_id` 的那些行有意义（第 1、2 行：李雷和他自己的订单），其余都是无意义的组合。**连接就是在笛卡尔积上加一个条件**，只保留配对正确的行。当然，数据库并不会真的先生成 30 行再过滤（表大了根本生成不出来），它会用更高效的方法找到配对的行，但结果和「先配对再过滤」是一样的。

## 3. 内连接和左连接

先看演示，分别点「内连接」和「左连接」：

<div class="dsviz" data-viz="sql-join"></div>

### 3.1 内连接：JOIN

```sql
SELECT c.name, o.id AS order_id, o.created_at
FROM customer c
JOIN orders o ON o.customer_id = c.id
ORDER BY o.id;
```

```
+--------+----------+---------------------+
| name   | order_id | created_at          |
+--------+----------+---------------------+
| 李雷   |        1 | 2026-10-01 09:12:00 |
| 李雷   |        2 | 2026-10-08 20:45:00 |
| 韩梅梅 |        3 | 2026-10-03 13:30:00 |
| 张三   |        4 | 2026-10-10 11:05:00 |
| 韩梅梅 |        5 | 2026-10-15 22:10:00 |
| 王芳   |        6 | 2026-10-20 08:00:00 |
+--------+----------+---------------------+
6 rows in set (0.00 sec)
```

`JOIN`（也写作 `INNER JOIN`）只保留两边都配对成功的行：每个订单一行。没有订单的赵六不在结果中。

> 也可以写成 `FROM customer c, orders o WHERE o.customer_id = c.id`，结果相同。但用 `JOIN … ON` 把「怎样连接」和「怎样过滤」分开，更清楚，也不容易漏写连接条件而得到笛卡尔积。

### 3.2 左连接：LEFT JOIN

想列出**所有**客户和他们的订单，包括没下过单的：

```sql
SELECT c.name, o.id AS order_id, o.created_at
FROM customer c
LEFT JOIN orders o ON o.customer_id = c.id
ORDER BY c.id, o.id;
```

```
+--------+----------+---------------------+
| name   | order_id | created_at          |
+--------+----------+---------------------+
| 李雷   |        1 | 2026-10-01 09:12:00 |
| 李雷   |        2 | 2026-10-08 20:45:00 |
| 韩梅梅 |        3 | 2026-10-03 13:30:00 |
| 韩梅梅 |        5 | 2026-10-15 22:10:00 |
| 张三   |        4 | 2026-10-10 11:05:00 |
| 王芳   |        6 | 2026-10-20 08:00:00 |
| 赵六   |     NULL | NULL                |
+--------+----------+---------------------+
7 rows in set (0.00 sec)
```

`LEFT JOIN` 保留**左表**（写在前面的 `customer`）的每一行。找不到配对的赵六也保留下来，右表的列都是 `NULL`。`RIGHT JOIN` 反过来保留右表的每一行，实际中很少用，把两个表交换位置写成 `LEFT JOIN` 更容易读。

利用这些 `NULL`，可以找出「没有下过订单的客户」：

```sql
SELECT c.name FROM customer c
LEFT JOIN orders o ON o.customer_id = c.id
WHERE o.id IS NULL;
```

```
+------+
| name |
+------+
| 赵六 |
+------+
```

### 3.3 条件写在 ON 还是 WHERE

需求：列出所有客户，以及他们 10 月 10 日以后的订单。把日期条件写在 `WHERE` 中：

```sql
SELECT c.name, o.id AS order_id, o.created_at
FROM customer c
LEFT JOIN orders o ON o.customer_id = c.id
WHERE o.created_at >= '2026-10-10'
ORDER BY c.id;
```

```
+--------+----------+---------------------+
| name   | order_id | created_at          |
+--------+----------+---------------------+
| 韩梅梅 |        5 | 2026-10-15 22:10:00 |
| 张三   |        4 | 2026-10-10 11:05:00 |
| 王芳   |        6 | 2026-10-20 08:00:00 |
+--------+----------+---------------------+
3 rows in set (0.00 sec)
```

只有 3 行，李雷和赵六不见了，`LEFT JOIN` 白写了。原因是执行顺序：`LEFT JOIN` 先得到 7 行（赵六那一行的 `created_at` 是 `NULL`），然后 `WHERE` 过滤，`NULL >= '2026-10-10'` 是未知，赵六被过滤掉；李雷的两个订单都在 10 日之前，也都被过滤掉了。

把条件写进 `ON`：

```sql
SELECT c.name, o.id AS order_id, o.created_at
FROM customer c
LEFT JOIN orders o ON o.customer_id = c.id AND o.created_at >= '2026-10-10'
ORDER BY c.id;
```

```
+--------+----------+---------------------+
| name   | order_id | created_at          |
+--------+----------+---------------------+
| 李雷   |     NULL | NULL                |
| 韩梅梅 |        5 | 2026-10-15 22:10:00 |
| 张三   |        4 | 2026-10-10 11:05:00 |
| 王芳   |        6 | 2026-10-20 08:00:00 |
| 赵六   |     NULL | NULL                |
+--------+----------+---------------------+
5 rows in set (0.00 sec)
```

5 个客户都在。`ON` 决定**哪些行能配对**，配不上的左表行仍然保留；`WHERE` 是在连接**之后**过滤结果。对左连接来说：

- 关于**右表**的条件，想「只配对符合条件的行」，写在 `ON` 中；
- 写在 `WHERE` 中会把右表为 `NULL` 的行过滤掉，左连接实际上变成了内连接。

对内连接来说，两种写法的结果相同。

## 4. 连接多个表

订单的明细：哪个客户、买了什么、多少钱。需要四个表，一个接一个地连接：

```sql
SELECT o.id AS 订单, c.name AS 客户, p.name AS 商品,
       i.quantity AS 数量, i.price AS 单价, i.quantity * i.price AS 小计
FROM orders o
JOIN customer c   ON c.id = o.customer_id
JOIN order_item i ON i.order_id = o.id
JOIN product p    ON p.id = i.product_id
ORDER BY o.id, p.id;
```

```
+------+--------+------------+------+---------+---------+
| 订单 | 客户   | 商品       | 数量 | 单价    | 小计    |
+------+--------+------------+------+---------+---------+
|    1 | 李雷   | 机械键盘   |    1 |  299.00 |  299.00 |
|    1 | 李雷   | 无线鼠标   |    2 |   89.90 |  179.80 |
|    2 | 李雷   | Type-C 线  |    3 |   19.90 |   59.70 |
|    3 | 韩梅梅 | 显示器     |    1 | 1299.00 | 1299.00 |
|    3 | 韩梅梅 | 笔记本支架 |    1 |  129.00 |  129.00 |
|    4 | 张三   | 降噪耳机   |    1 | 1499.00 | 1499.00 |
|    5 | 韩梅梅 | 无线鼠标   |    1 |   89.90 |   89.90 |
|    5 | 韩梅梅 | USB 集线器 |    2 |   59.00 |  118.00 |
|    5 | 韩梅梅 | Type-C 线  |    5 |   19.90 |   99.50 |
|    6 | 王芳   | 机械键盘   |    2 |  279.00 |  558.00 |
+------+--------+------------+------+---------+---------+
10 rows in set (0.00 sec)
```

每连接一个表，就多一个 `JOIN … ON`。订单 6 中机械键盘的单价是 279，不是商品表中现在的 299：`order_item.price` 记录的是**下单时**的价格（也许当时打了折），商品以后调价不应该改变已经成交的订单。这是有意的「重复存储」（2.1 篇）。

在连接的结果上分组，得到每个订单的金额：

```sql
SELECT o.id AS 订单, c.name AS 客户, COUNT(*) AS 商品种数, SUM(i.quantity * i.price) AS 金额
FROM orders o
JOIN customer c   ON c.id = o.customer_id
JOIN order_item i ON i.order_id = o.id
GROUP BY o.id, c.name
ORDER BY 金额 DESC;
```

```
+------+--------+----------+---------+
| 订单 | 客户   | 商品种数 | 金额    |
+------+--------+----------+---------+
|    4 | 张三   |        1 | 1499.00 |
|    3 | 韩梅梅 |        2 | 1428.00 |
|    6 | 王芳   |        1 |  558.00 |
|    1 | 李雷   |        2 |  478.80 |
|    5 | 韩梅梅 |        3 |  307.40 |
|    2 | 李雷   |        1 |   59.70 |
+------+--------+----------+---------+
```

### 4.1 连接后统计：行被放大了

每个客户的订单数和消费总额，包括没下过单的客户：

```sql
SELECT c.name, COUNT(o.id) AS 订单数, COALESCE(SUM(i.quantity * i.price), 0) AS 消费总额
FROM customer c
LEFT JOIN orders o     ON o.customer_id = c.id
LEFT JOIN order_item i ON i.order_id = o.id
GROUP BY c.id, c.name
ORDER BY 消费总额 DESC;
```

```
+--------+--------+----------+
| name   | 订单数 | 消费总额 |
+--------+--------+----------+
| 韩梅梅 |      5 |  1735.40 |
| 张三   |      1 |  1499.00 |
| 王芳   |      1 |   558.00 |
| 李雷   |      3 |   538.50 |
| 赵六   |      0 |     0.00 |
+--------+--------+----------+
```

消费总额是对的，订单数却错了：韩梅梅只有 2 个订单，这里是 5；李雷 2 个，这里是 3。原因是连接了 `order_item` 以后，**每个订单有几件商品，就有几行**：韩梅梅的订单 3 有 2 行明细，订单 5 有 3 行，连接后她一共 5 行，`COUNT(o.id)` 数的是行数，不是订单数。

一对多的连接会「放大」左边的行。在放大后的结果上数「一」那一边的东西，要去重：

```sql
SELECT c.name, COUNT(DISTINCT o.id) AS 订单数, COALESCE(SUM(i.quantity * i.price), 0) AS 消费总额
...
```

```
+--------+--------+----------+
| name   | 订单数 | 消费总额 |
+--------+--------+----------+
| 韩梅梅 |      2 |  1735.40 |
| 张三   |      1 |  1499.00 |
| 王芳   |      1 |   558.00 |
| 李雷   |      2 |   538.50 |
| 赵六   |      0 |     0.00 |
+--------+--------+----------+
```

`SUM` 就没有这么简单的补救办法了：如果要对「一」那一边的列求和（例如订单表中有一个运费列），连接后它会被重复加上几次。这时应该先在子查询中把「多」的一边汇总成每个订单一行，再连接（第 5.4 节的派生表）。

`COALESCE(x, 0)` 返回第一个不为 `NULL` 的参数：赵六没有订单，`SUM` 的结果是 `NULL`，显示成 0。

## 5. 子查询

一个 `SELECT` 可以出现在另一条语句中，叫作**子查询**。

### 5.1 标量子查询

返回一个值的子查询，可以用在任何需要一个值的地方。价格高于平均价的商品：

```sql
SELECT name, price FROM product WHERE price > (SELECT AVG(price) FROM product);
```

```
+----------+---------+
| name     | price   |
+----------+---------+
| 显示器   | 1299.00 |
| 便携屏   |  899.00 |
| 降噪耳机 | 1499.00 |
+----------+---------+
```

`WHERE` 中不能直接用 `AVG`（上一篇），先用子查询算出平均价（521.42）就可以了。

### 5.2 IN

返回一列值的子查询，配合 `IN` 使用。买过外设的客户：

```sql
SELECT name FROM customer WHERE id IN (
    SELECT o.customer_id
    FROM orders o
    JOIN order_item i ON i.order_id = o.id
    JOIN product p    ON p.id = i.product_id
    WHERE p.category = '外设'
);
```

```
+--------+
| name   |
+--------+
| 李雷   |
| 韩梅梅 |
| 王芳   |
+--------+
```

### 5.3 EXISTS 和关联子查询

`EXISTS (子查询)` 判断子查询是否返回了至少一行。从来没有卖出过的商品：

```sql
SELECT name, stock FROM product p
WHERE NOT EXISTS (SELECT 1 FROM order_item i WHERE i.product_id = p.id);
```

```
+------------+-------+
| name       | stock |
+------------+-------+
| 便携屏     |     0 |
| 蓝牙音箱   |     0 |
| 新品摄像头 |     0 |
+------------+-------+
```

子查询中用到了外层的 `p.id`，这叫**关联子查询**：逻辑上，外层的每一行都要把自己的 `p.id` 代入，执行一次子查询。`SELECT 1` 中的 1 没有意义，`EXISTS` 只关心有没有行。

关联子查询也可以返回一个值。每个分类中最贵的商品：

```sql
SELECT name, category, price FROM product p
WHERE price = (SELECT MAX(price) FROM product WHERE category = p.category);
```

```
+------------+----------+---------+
| name       | category | price   |
+------------+----------+---------+
| 机械键盘   | 外设     |  299.00 |
| 显示器     | 显示     | 1299.00 |
| 笔记本支架 | 配件     |  129.00 |
| 降噪耳机   | 音频     | 1499.00 |
+------------+----------+---------+
```

### 5.4 派生表

`FROM` 中的子查询，它的结果当作一个临时的表来用，必须起别名：

```sql
SELECT t.category, t.avg_price
FROM (SELECT category, AVG(price) AS avg_price FROM product GROUP BY category) AS t
WHERE t.avg_price > 500;
```

```
+----------+-------------+
| category | avg_price   |
+----------+-------------+
| 显示     | 1099.000000 |
| 音频     |  949.000000 |
+----------+-------------+
```

这个例子用 `HAVING` 也能做到，派生表的用处在于把复杂的查询分成几步：先汇总，再在汇总的结果上连接、过滤。

### 5.5 NOT IN 和 NULL

「不在上海的客户」：

```sql
SELECT name, city FROM customer WHERE city NOT IN ('上海');
```

```
+------+------+
| name | city |
+------+------+
| 李雷 | 北京 |
| 张三 | 北京 |
| 王芳 | 广州 |
+------+------+
```

城市为 `NULL` 的赵六不在结果中，这是上一篇的三值逻辑。更意外的是列表中有 `NULL` 的情况：

```sql
SELECT name FROM customer WHERE city NOT IN ('上海', NULL);
```

```
Empty set (0.00 sec)
```

一行都没有。`city NOT IN ('上海', NULL)` 等于 `city <> '上海' AND city <> NULL`，而 `city <> NULL` 永远是未知，整个条件不可能为真。

直接写出 `NULL` 的人很少，但子查询中混进 `NULL` 很常见。一个黑名单表中有客户 2，还有一行 `customer_id` 是 `NULL`（例如录入时出错）：

```sql
CREATE TEMPORARY TABLE blacklist (customer_id INT UNSIGNED);
INSERT INTO blacklist VALUES (2), (NULL);
```

`TEMPORARY` 表只在当前连接中存在，断开后自动删除，适合做这样的试验。查「不在黑名单中的客户」：

```sql
SELECT name FROM customer WHERE id NOT IN (SELECT customer_id FROM blacklist);
```

```
Empty set (0.00 sec)
```

全部客户都消失了，没有报错，也没有警告。用 `NOT EXISTS` 改写：

```sql
SELECT name FROM customer c
WHERE NOT EXISTS (SELECT 1 FROM blacklist b WHERE b.customer_id = c.id);
```

```
+------+
| name |
+------+
| 李雷 |
| 张三 |
| 王芳 |
| 赵六 |
+------+
```

正确地排除了韩梅梅。`NOT EXISTS` 只关心「有没有配对的行」，`NULL` 配不上任何客户，不影响结果。所以：**子查询的列可能为 `NULL` 时，用 `NOT EXISTS`，不要用 `NOT IN`**。（`SELECT customer_id FROM orders` 不会有这个问题，因为 `orders.customer_id` 定义为 `NOT NULL`，这也是给列加上 `NOT NULL` 约束的好处之一。）

## 6. UNION

`UNION` 把两个查询的结果上下拼在一起，要求列数相同：

```sql
SELECT city FROM customer UNION SELECT '深圳';
SELECT city FROM customer UNION ALL SELECT '深圳';
```

```
+------+
| city |
+------+
| 北京 |
| 上海 |
| 广州 |
| NULL |
| 深圳 |
+------+
5 rows in set (0.00 sec)

+------+
| city |
+------+
| 北京 |
| 上海 |
| 北京 |
| 广州 |
| NULL |
| 深圳 |
+------+
6 rows in set (0.00 sec)
```

`UNION` 会去掉重复的行（两个北京只剩一个），`UNION ALL` 保留全部。去重需要额外的排序或哈希，所以确定不会重复、或者不需要去重时，用 `UNION ALL`。

两个查询的列可以来自不同的表，结果的列名取第一个查询的：

```sql
SELECT name, '客户' AS 类型 FROM customer WHERE city = '北京'
UNION ALL
SELECT name, '商品' FROM product WHERE price > 1000;
```

```
+----------+------+
| name     | 类型 |
+----------+------+
| 李雷     | 客户 |
| 张三     | 客户 |
| 显示器   | 商品 |
| 降噪耳机 | 商品 |
+----------+------+
```

## 7. 常见错误

- **漏写连接条件**：得到笛卡尔积，行数是两个表行数的乘积，表大时可能把数据库拖垮；
- **左连接的右表条件写在 `WHERE` 中**：左连接变成了内连接；
- **一对多连接后用 `COUNT`、`SUM` 统计「一」那一边**：行被放大，结果偏大，用 `COUNT(DISTINCT …)` 或者先汇总再连接；
- **`NOT IN` 的子查询可能返回 `NULL`**：结果为空，用 `NOT EXISTS`；
- **多个表中有同名的列却不加表名**：`ERROR 1052 (23000): Column 'id' in field list is ambiguous`。连接时所有的列都写成 `表别名.列名`；
- **需要去重时用了 `UNION ALL`，或者反过来**。

## 8. 小结

- 数据拆成多个表，避免重复；查询时用连接组合起来
- 连接在逻辑上是「笛卡尔积 + 条件」；`JOIN` 只保留配对成功的行，`LEFT JOIN` 保留左表的每一行，配不上的填 `NULL`
- `LEFT JOIN … WHERE 右表.列 IS NULL` 找出「没有对应记录」的行
- 左连接中，右表的条件写在 `ON` 中；写在 `WHERE` 中会变成内连接
- 一对多连接会放大行数，统计时注意去重
- 子查询：标量子查询、`IN`、`EXISTS`、关联子查询、派生表；可能有 `NULL` 时用 `NOT EXISTS` 而不是 `NOT IN`
- `UNION` 去重，`UNION ALL` 不去重

## 9. 练习

1. 统计每件商品卖出的总数量，没有卖出的显示 0，按销量从高到低排列。
2. 统计每个城市的客户数和订单数，城市为空的显示为「未知」。
3. 第 5.2 节「买过外设的客户」，用连接（不用子查询）怎样写？要注意什么？

<details>
<summary>点击查看答案</summary>

1. 以商品表为左表左连接订单明细，没有卖出的商品也保留下来：

   ```sql
   SELECT p.name, COALESCE(SUM(i.quantity), 0) AS 销量
   FROM product p
   LEFT JOIN order_item i ON i.product_id = p.id
   GROUP BY p.id, p.name
   ORDER BY 销量 DESC, p.id;
   ```

   ```
   +------------+------+
   | name       | 销量 |
   +------------+------+
   | Type-C 线  |    8 |
   | 机械键盘   |    3 |
   | 无线鼠标   |    3 |
   | USB 集线器 |    2 |
   | 显示器     |    1 |
   | 笔记本支架 |    1 |
   | 降噪耳机   |    1 |
   | 便携屏     |    0 |
   | 蓝牙音箱   |    0 |
   | 新品摄像头 |    0 |
   +------------+------+
   ```

   `ORDER BY` 中的第二个排序列 `p.id` 让销量相同的商品有确定的顺序。
2. 客户左连接订单，按城市分组。客户数要去重（一个客户有几个订单就有几行），订单数用 `COUNT(o.id)`（没有订单时 `o.id` 是 `NULL`，不被计数）：

   ```sql
   SELECT COALESCE(c.city, '未知') AS 城市, COUNT(DISTINCT c.id) AS 客户数, COUNT(o.id) AS 订单数
   FROM customer c
   LEFT JOIN orders o ON o.customer_id = c.id
   GROUP BY c.city
   ORDER BY 订单数 DESC;
   ```

   ```
   +------+--------+--------+
   | 城市 | 客户数 | 订单数 |
   +------+--------+--------+
   | 北京 |      2 |      3 |
   | 上海 |      1 |      2 |
   | 广州 |      1 |      1 |
   | 未知 |      1 |      0 |
   +------+--------+--------+
   ```

   `GROUP BY` 把所有 `NULL` 归为一组，赵六单独成了「未知」组。
3. 直接连接四个表：

   ```sql
   SELECT c.name
   FROM customer c
   JOIN orders o     ON o.customer_id = c.id
   JOIN order_item i ON i.order_id = o.id
   JOIN product p    ON p.id = i.product_id
   WHERE p.category = '外设';
   ```

   ```
   +--------+
   | name   |
   +--------+
   | 李雷   |
   | 王芳   |
   | 李雷   |
   | 韩梅梅 |
   +--------+
   ```

   李雷出现了两次：他的订单 1 中有两件外设（键盘和鼠标），连接后是两行。要加上 `DISTINCT`（`SELECT DISTINCT c.name`），结果才和子查询的版本一样是 3 行。`IN` 子查询只判断「是否在其中」，天然不会重复；连接会按配对的行数重复。这是第 4.1 节的「放大」的另一种表现。

</details>
