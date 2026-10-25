---
title: 增删改查
date: 2026-10-25 09:20:00
updated: 2026-10-25 09:20:00
categories:
  - 数据库
  - SQL基础
tags:
  - 数据库
  - MySQL
  - SQL
order: 3
description: 在一个网店的商品表上练习单表的增删改查：条件、排序、分页、去重、聚合、分组。重点是几个容易出错的地方：AND 比 OR 优先，少了括号就多查出 2 行；price = NULL 什么都查不到，价格小于 100 和不小于 100 的商品加起来只有 9 件，另一件哪去了；AVG 会跳过 NULL；WHERE 中不能用别名和聚合函数，配有一条 SELECT 语句按执行顺序逐步运行的演示；最后用安全更新模式防止忘写 WHERE 的 UPDATE。
top_group_list: true
disableNunjucks: true
viz: [db-select]
---

> **本文目标**：会用 `INSERT`、`UPDATE`、`DELETE` 修改数据；会写单表的 `SELECT`：条件、排序、分页、去重；会用聚合函数和 `GROUP BY`、`HAVING` 统计；理解 `NULL` 的三值逻辑；理解 `SELECT` 语句的执行顺序。
>
> **前置知识**：上一篇《建表与数据类型》。
>
> **实验环境**：MySQL 8.4.13（Docker），客户端字符集 utf8mb4。

## 1. 示例数据

本文和下一篇使用一个网店的数据库 `shop`，有四个表：客户 `customer`、商品 `product`、订单 `orders`、订单明细 `order_item`。本文只用商品表，其他三个表下一篇再用。完整的建表和数据脚本在本节末尾，下面是商品表的全部数据：

```
+----+------------+----------+---------+-------+
| id | name       | category | price   | stock |
+----+------------+----------+---------+-------+
|  1 | 机械键盘   | 外设     |  299.00 |    50 |
|  2 | 无线鼠标   | 外设     |   89.90 |   200 |
|  3 | 显示器     | 显示     | 1299.00 |    15 |
|  4 | 便携屏     | 显示     |  899.00 |     0 |
|  5 | USB 集线器 | 配件     |   59.00 |   120 |
|  6 | Type-C 线  | 配件     |   19.90 |   500 |
|  7 | 笔记本支架 | 配件     |  129.00 |    35 |
|  8 | 降噪耳机   | 音频     | 1499.00 |     8 |
|  9 | 蓝牙音箱   | 音频     |  399.00 |     0 |
| 10 | 新品摄像头 | 外设     |    NULL |     0 |
+----+------------+----------+---------+-------+
```

「新品摄像头」还没有定价，`price` 是 `NULL`。它是故意放进来的，后面会看到 `NULL` 带来的各种意外。

<details>
<summary>shop.sql：建表和数据</summary>

```sql shop.sql
DROP DATABASE IF EXISTS shop;
CREATE DATABASE shop;
USE shop;

CREATE TABLE customer (
    id         INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    name       VARCHAR(50) NOT NULL,
    city       VARCHAR(20),
    created_at DATE NOT NULL
);

CREATE TABLE product (
    id       INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    name     VARCHAR(50) NOT NULL,
    category VARCHAR(20) NOT NULL,
    price    DECIMAL(10, 2),
    stock    INT UNSIGNED NOT NULL DEFAULT 0
);

CREATE TABLE orders (
    id          INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    customer_id INT UNSIGNED NOT NULL,
    created_at  DATETIME NOT NULL,
    FOREIGN KEY (customer_id) REFERENCES customer (id)
);

CREATE TABLE order_item (
    order_id   INT UNSIGNED NOT NULL,
    product_id INT UNSIGNED NOT NULL,
    quantity   INT UNSIGNED NOT NULL,
    price      DECIMAL(10, 2) NOT NULL,          -- 下单时的价格
    PRIMARY KEY (order_id, product_id),
    FOREIGN KEY (order_id) REFERENCES orders (id),
    FOREIGN KEY (product_id) REFERENCES product (id)
);

INSERT INTO customer (name, city, created_at) VALUES
    ('李雷', '北京', '2026-01-05'),
    ('韩梅梅', '上海', '2026-02-11'),
    ('张三', '北京', '2026-03-20'),
    ('王芳', '广州', '2026-05-02'),
    ('赵六', NULL, '2026-09-30');

INSERT INTO product (name, category, price, stock) VALUES
    ('机械键盘', '外设', 299.00, 50),
    ('无线鼠标', '外设', 89.90, 200),
    ('显示器', '显示', 1299.00, 15),
    ('便携屏', '显示', 899.00, 0),
    ('USB 集线器', '配件', 59.00, 120),
    ('Type-C 线', '配件', 19.90, 500),
    ('笔记本支架', '配件', 129.00, 35),
    ('降噪耳机', '音频', 1499.00, 8),
    ('蓝牙音箱', '音频', 399.00, 0),
    ('新品摄像头', '外设', NULL, 0);

INSERT INTO orders (customer_id, created_at) VALUES
    (1, '2026-10-01 09:12:00'),
    (1, '2026-10-08 20:45:00'),
    (2, '2026-10-03 13:30:00'),
    (3, '2026-10-10 11:05:00'),
    (2, '2026-10-15 22:10:00'),
    (4, '2026-10-20 08:00:00');

INSERT INTO order_item VALUES
    (1, 1, 1, 299.00), (1, 2, 2, 89.90),
    (2, 6, 3, 19.90),
    (3, 3, 1, 1299.00), (3, 7, 1, 129.00),
    (4, 8, 1, 1499.00),
    (5, 2, 1, 89.90), (5, 5, 2, 59.00), (5, 6, 5, 19.90),
    (6, 1, 2, 279.00);
```

保存为 `shop.sql` 后执行 `mysql < shop.sql`（在客户端中可以用 `SOURCE shop.sql`）。它会先删除已有的 `shop` 库，上一篇建的同名表会被替换。

</details>

## 2. 查询：SELECT

### 2.1 选择列，计算表达式

```sql
SELECT name, price, stock, price * stock AS 库存金额 FROM product WHERE stock > 0;
```

```
+------------+---------+-------+----------+
| name       | price   | stock | 库存金额 |
+------------+---------+-------+----------+
| 机械键盘   |  299.00 |    50 | 14950.00 |
| 无线鼠标   |   89.90 |   200 | 17980.00 |
| 显示器     | 1299.00 |    15 | 19485.00 |
| USB 集线器 |   59.00 |   120 |  7080.00 |
| Type-C 线  |   19.90 |   500 |  9950.00 |
| 笔记本支架 |  129.00 |    35 |  4515.00 |
| 降噪耳机   | 1499.00 |     8 | 11992.00 |
+------------+---------+-------+----------+
7 rows in set (0.00 sec)
```

`SELECT` 后面可以是列，也可以是表达式；`AS` 给结果的列起一个**别名**（`AS` 可以省略）。实际的程序中应该写出需要的列，而不是 `SELECT *`：表增加了列，`*` 会多返回不需要的数据，依赖列的顺序的代码也可能出错。

### 2.2 条件：WHERE

| 运算符 | 含义 | 例子 |
|---|---|---|
| `=`、`<>`（或 `!=`）、`<`、`<=`、`>`、`>=` | 比较 | `price >= 100` |
| `AND`、`OR`、`NOT` | 逻辑运算 | `stock > 0 AND price < 100` |
| `IN (…)` | 等于其中之一 | `category IN ('显示', '音频')` |
| `BETWEEN a AND b` | 在 a 和 b 之间（包含两端） | `price BETWEEN 500 AND 1300` |
| `LIKE` | 模式匹配：`%` 任意多个字符，`_` 一个字符 | `name LIKE '%机%'` |
| `IS NULL`、`IS NOT NULL` | 是否为空 | `price IS NULL` |

```sql
SELECT name, price FROM product WHERE category IN ('显示', '音频') AND price BETWEEN 500 AND 1300;
SELECT name FROM product WHERE name LIKE '%机%';
SELECT name FROM product WHERE name LIKE '_线%';
```

```
+--------+---------+
| name   | price   |
+--------+---------+
| 显示器 | 1299.00 |
| 便携屏 |  899.00 |
+--------+---------+

+----------+
| name     |
+----------+
| 机械键盘 |
| 降噪耳机 |
+----------+

+----------+
| name     |
+----------+
| 无线鼠标 |
+----------+
```

`'_线%'` 要求第二个字符是「线」，所以「Type-C 线」不满足。

**`AND` 比 `OR` 优先。** 想找「外设或配件中，价格低于 100 的商品」：

```sql
SELECT name, category, price FROM product
WHERE category = '外设' OR category = '配件' AND price < 100;
```

```
+------------+----------+--------+
| name       | category | price  |
+------------+----------+--------+
| 机械键盘   | 外设     | 299.00 |
| 无线鼠标   | 外设     |  89.90 |
| USB 集线器 | 配件     |  59.00 |
| Type-C 线  | 配件     |  19.90 |
| 新品摄像头 | 外设     |   NULL |
+------------+----------+--------+
5 rows in set (0.00 sec)
```

299 元的键盘也出来了。这个条件实际上是 `category = '外设' OR (category = '配件' AND price < 100)`：所有的外设，加上便宜的配件。加上括号：

```sql
SELECT name, category, price FROM product
WHERE (category = '外设' OR category = '配件') AND price < 100;
```

```
+------------+----------+-------+
| name       | category | price |
+------------+----------+-------+
| 无线鼠标   | 外设     | 89.90 |
| USB 集线器 | 配件     | 59.00 |
| Type-C 线  | 配件     | 19.90 |
+------------+----------+-------+
3 rows in set (0.00 sec)
```

`AND` 和 `OR` 混用时，总是加上括号。

### 2.3 NULL

`NULL` 表示「未知」或「没有值」，它不是 0，也不是空字符串。和 `NULL` 做任何比较、运算，结果都是 `NULL`（未知）：

```sql
SELECT NULL = NULL, NULL + 1, NULL IS NULL, 1 > NULL;
```

```
+-------------+----------+--------------+----------+
| NULL = NULL | NULL + 1 | NULL IS NULL | 1 > NULL |
+-------------+----------+--------------+----------+
|        NULL |     NULL |            1 |     NULL |
+-------------+----------+--------------+----------+
```

「一个未知的值等于另一个未知的值吗？」答案是「未知」。`WHERE` 只保留条件为**真**的行，条件为假或者未知的行都被过滤掉。所以：

```sql
SELECT name, price FROM product WHERE price = NULL;
SELECT name, price FROM product WHERE price IS NULL;
```

```
Empty set (0.00 sec)

+------------+-------+
| name       | price |
+------------+-------+
| 新品摄像头 |  NULL |
+------------+-------+
```

`price = NULL` 对每一行都是未知，一行也查不到，而且不会有任何报错。判断是否为空**必须用 `IS NULL`**。

再看一个更隐蔽的例子。价格低于 100 的商品，和价格**不**低于 100 的商品：

```sql
SELECT name, price FROM product WHERE price < 100;
SELECT name, price FROM product WHERE NOT (price < 100);
```

```
3 rows in set (0.00 sec)
6 rows in set (0.00 sec)
```

3 + 6 = 9，但表中有 10 件商品。新品摄像头的 `price < 100` 是未知，`NOT` 未知仍然是未知，所以它两边都不在。在 SQL 中，「满足条件」和「不满足条件」加起来不一定是全部，这就是**三值逻辑**（真、假、未知）。写条件时要想一想：这一列可能是 `NULL` 吗？`NULL` 的行应该归到哪一边？

### 2.4 排序：ORDER BY

```sql
SELECT name, category, price FROM product ORDER BY category, price DESC;
```

```
+------------+----------+---------+
| name       | category | price   |
+------------+----------+---------+
| 机械键盘   | 外设     |  299.00 |
| 无线鼠标   | 外设     |   89.90 |
| 新品摄像头 | 外设     |    NULL |
| 显示器     | 显示     | 1299.00 |
| 便携屏     | 显示     |  899.00 |
| 笔记本支架 | 配件     |  129.00 |
| USB 集线器 | 配件     |   59.00 |
| Type-C 线  | 配件     |   19.90 |
| 降噪耳机   | 音频     | 1499.00 |
| 蓝牙音箱   | 音频     |  399.00 |
+------------+----------+---------+
```

先按 `category` 升序（`ASC`，默认），分类相同的再按 `price` 降序（`DESC`）。两点值得注意：

- `NULL` 被当作最小的值：升序时排在最前，降序时排在最后；
- 分类的顺序是「外设、显示、配件、音频」，既不是拼音顺序（配、外、显、音），也不是笔画顺序。默认的排序规则 `utf8mb4_0900_ai_ci` 对汉字按 Unicode 编码排序（外 U+5916、显 U+663E、配 U+914D、音 U+97F3）。要按拼音排序，可以指定中文的排序规则：`ORDER BY category COLLATE utf8mb4_zh_0900_as_cs`，结果是「配件、外设、显示、音频」。

**没有 `ORDER BY`，结果的顺序是不确定的。** 上面的结果看起来按 `id` 排列，那只是 InnoDB 按主键存储的副作用，执行方式变了（例如使用了别的索引），顺序就可能改变。需要顺序时一定要写 `ORDER BY`。

### 2.5 分页：LIMIT

```sql
SELECT name, price FROM product ORDER BY price DESC LIMIT 3;     -- 最贵的 3 件
SELECT id, name FROM product ORDER BY id LIMIT 3 OFFSET 3;      -- 跳过 3 行，取 3 行
```

```
+----------+---------+
| name     | price   |
+----------+---------+
| 降噪耳机 | 1499.00 |
| 显示器   | 1299.00 |
| 便携屏   |  899.00 |
+----------+---------+

+----+------------+
| id | name       |
+----+------------+
|  4 | 便携屏     |
|  5 | USB 集线器 |
|  6 | Type-C 线  |
+----+------------+
```

每页 n 行，第 p 页就是 `LIMIT n OFFSET (p - 1) * n`（MySQL 也接受 `LIMIT 偏移, 行数` 的写法）。分页必须配合 `ORDER BY`，否则每一页的内容都不确定，可能重复或者遗漏。

偏移量很大时（第 10000 页），数据库仍然要先找出前面的所有行再丢弃，会越来越慢。第 3 章讲索引时再讨论更好的分页方式。

### 2.6 去重：DISTINCT

```sql
SELECT DISTINCT category FROM product;
```

```
+----------+
| category |
+----------+
| 外设     |
| 显示     |
| 配件     |
| 音频     |
+----------+
```

`DISTINCT` 作用于所有选出的列：`SELECT DISTINCT category, stock` 去掉的是这两列都相同的行。

## 3. 聚合和分组

### 3.1 聚合函数

聚合函数把多行计算成一个值：

```sql
SELECT COUNT(*), COUNT(price), SUM(stock), AVG(price), MAX(price), MIN(price) FROM product;
```

```
+----------+--------------+------------+------------+------------+------------+
| COUNT(*) | COUNT(price) | SUM(stock) | AVG(price) | MAX(price) | MIN(price) |
+----------+--------------+------------+------------+------------+------------+
|       10 |            9 |        928 | 521.422222 |    1499.00 |      19.90 |
+----------+--------------+------------+------------+------------+------------+
```

`COUNT(*)` 统计行数，是 10；`COUNT(price)` 统计 `price` **不为 `NULL`** 的行数，是 9。除了 `COUNT(*)`，所有的聚合函数都会跳过 `NULL`：`AVG(price)` 是 9 件商品的平均价。自己用总和除以总数：

```sql
SELECT SUM(price) / COUNT(*) FROM product;
```

```
+-----------------------+
| SUM(price) / COUNT(*) |
+-----------------------+
|            469.280000 |
+-----------------------+
```

结果不同。哪个对，取决于问题：「已定价商品的平均价」是 521.42，把未定价的当作 0 元算是 469.28。要清楚自己要的是哪一个。

### 3.2 GROUP BY

`GROUP BY` 把某一列（或几列）相同的行分成一组，聚合函数对每一组分别计算：

```sql
SELECT category, COUNT(*) AS 商品数, SUM(stock) AS 总库存, AVG(price) AS 均价
FROM product GROUP BY category;
```

```
+----------+--------+--------+-------------+
| category | 商品数 | 总库存 | 均价        |
+----------+--------+--------+-------------+
| 外设     |      3 |    250 |  194.450000 |
| 显示     |      2 |     15 | 1099.000000 |
| 配件     |      3 |    655 |   69.300000 |
| 音频     |      2 |      8 |  949.000000 |
+----------+--------+--------+-------------+
```

外设有 3 件商品，均价却是 (299 + 89.9) / 2 = 194.45：新品摄像头又被跳过了。

分组以后，结果中的每一行代表一**组**。如果在 `SELECT` 中写一个既不是分组列、也不在聚合函数中的列：

```sql
SELECT category, name, COUNT(*) FROM product GROUP BY category;
```

```
ERROR 1055 (42000): Expression #2 of SELECT list is not in GROUP BY clause and contains nonaggregated column 'shop.product.name' which is not functionally dependent on columns in GROUP BY clause; this is incompatible with sql_mode=only_full_group_by
```

外设组有 3 个商品名，这一行该显示哪一个？无法回答，所以报错（上一篇 `sql_mode` 中的 `ONLY_FULL_GROUP_BY`）。老版本的 MySQL 不检查这一点，会随便取组中某一行的值，结果看起来正常，其实是错的。

### 3.3 HAVING

「有库存的商品中，哪些分类有 2 件以上？按件数从多到少排列」：

```sql
SELECT category, COUNT(*) AS n
FROM product
WHERE stock > 0
GROUP BY category
HAVING n >= 2
ORDER BY n DESC;
```

```
+----------+---+
| category | n |
+----------+---+
| 配件     | 3 |
| 外设     | 2 |
+----------+---+
```

`WHERE` 和 `HAVING` 都是过滤，区别在于过滤的时机：`WHERE` 在分组**之前**过滤行，`HAVING` 在分组**之后**过滤组。所以 `WHERE` 中不能使用聚合函数：

```sql
SELECT category, COUNT(*) FROM product WHERE COUNT(*) >= 3 GROUP BY category;
```

```
ERROR 1111 (HY000): Invalid use of group function
```

## 4. SELECT 的执行顺序

上面这条查询的书写顺序是 `SELECT … FROM … WHERE … GROUP BY … HAVING … ORDER BY`，但数据库在逻辑上是按另一个顺序处理的。点「运行」逐步查看：

<div class="dsviz" data-viz="sql-order"></div>

| 顺序 | 子句 | 作用 |
|---|---|---|
| 1 | `FROM` | 确定数据来源（下一篇还会在这里连接多个表） |
| 2 | `WHERE` | 过滤行 |
| 3 | `GROUP BY` | 分组，计算聚合函数 |
| 4 | `HAVING` | 过滤组 |
| 5 | `SELECT` | 计算输出的列和别名，`DISTINCT` 去重 |
| 6 | `ORDER BY` | 排序 |
| 7 | `LIMIT` | 取其中的一部分 |

记住这个顺序，很多规则就自然理解了。例如 `SELECT` 中定义的别名，在 `WHERE` 中还不存在：

```sql
SELECT name, price * 0.9 AS sale FROM product WHERE sale < 100;
```

```
ERROR 1054 (42S22): Unknown column 'sale' in 'where clause'
```

要写成 `WHERE price * 0.9 < 100`。而 `ORDER BY` 在 `SELECT` 之后，可以使用别名（MySQL 还额外允许在 `HAVING` 中使用别名，3.3 节的 `HAVING n >= 2` 就是这样，标准 SQL 中要写 `HAVING COUNT(*) >= 2`）。

这是**逻辑上**的顺序，规定了结果应该是什么。数据库实际执行时可以换一种更快的方式（例如用索引直接按顺序读出，省掉排序），只要结果相同。

## 5. 增加、修改、删除

### 5.1 INSERT

```sql
INSERT INTO product (name, category, price) VALUES ('游戏手柄', '外设', 239.00);
SELECT * FROM product WHERE id = LAST_INSERT_ID();
```

```
+----+----------+----------+--------+-------+
| id | name     | category | price  | stock |
+----+----------+----------+--------+-------+
| 11 | 游戏手柄 | 外设     | 239.00 |     0 |
+----+----------+----------+--------+-------+
```

写出列名，只给需要的列赋值，其他列取默认值（`id` 自增，`stock` 默认为 0）。`LAST_INSERT_ID()` 返回当前连接最后一次插入生成的自增值，程序中常用它得到新行的 `id`。

上一篇用的是不写列名的 `INSERT INTO product VALUES (...)`，它要求按表中列的顺序给出每一列的值，表结构一改就会出错，程序中应该总是写出列名。

「如果不存在就插入，存在就更新」可以用 `ON DUPLICATE KEY UPDATE`。例如进货：无线鼠标（`id` 为 2）入库 100 个：

```sql
INSERT INTO product (id, name, category, price, stock) VALUES (2, '无线鼠标', '外设', 89.90, 100)
    AS new ON DUPLICATE KEY UPDATE stock = product.stock + new.stock;
SELECT name, stock FROM product WHERE id = 2;
```

```
Query OK, 2 rows affected (0.00 sec)

+----------+-------+
| name     | stock |
+----------+-------+
| 无线鼠标 |   300 |
+----------+-------+
```

主键 2 已经存在，于是执行后面的 `UPDATE`：原来的 200 加上新的 100。`AS new` 给要插入的这一行起了个名字，`new.stock` 就是 100。`2 rows affected` 是 MySQL 的约定：插入了新行是 1，更新了已有的行是 2。

### 5.2 UPDATE

```sql
UPDATE product SET price = price * 0.9 WHERE category = '配件';
UPDATE product SET stock = 0 WHERE id = 4;
UPDATE product SET stock = stock + 30, price = 1199.00 WHERE name = '显示器';
```

```
Query OK, 3 rows affected (0.00 sec)
Rows matched: 3  Changed: 3  Warnings: 0

Query OK, 0 rows affected (0.00 sec)
Rows matched: 1  Changed: 0  Warnings: 0

Query OK, 1 row affected (0.00 sec)
Rows matched: 1  Changed: 1  Warnings: 0
```

`Rows matched` 是满足 `WHERE` 的行数，`Changed` 是值真正改变了的行数。便携屏的库存本来就是 0，匹配了 1 行，但没有改变。

### 5.3 DELETE

```sql
DELETE FROM product WHERE price IS NULL;
DELETE FROM product WHERE id = 1;
```

```
Query OK, 1 row affected (0.00 sec)

ERROR 1451 (23000): Cannot delete or update a parent row: a foreign key constraint fails (`shop`.`order_item`, CONSTRAINT `order_item_ibfk_2` FOREIGN KEY (`product_id`) REFERENCES `product` (`id`))
```

未定价的新品摄像头删掉了；机械键盘出现在订单明细中，外键阻止了删除。

### 5.4 忘写 WHERE

`UPDATE` 和 `DELETE` 最危险的错误是忘写 `WHERE`，或者 `WHERE` 写错了范围。少写一行，就是全表：

```sql
UPDATE product SET price = 0;
```

```
Query OK, 10 rows affected (0.01 sec)
Rows matched: 10  Changed: 10  Warnings: 0
```

所有商品都成了 0 元，而且无法撤销（除非在事务中，第 4 章）。几个防范的办法：

- 执行 `UPDATE`、`DELETE` 之前，先用同样的 `WHERE` 执行一次 `SELECT`，确认影响的是哪些行；
- 客户端启动时加上 `--safe-updates`（或者执行 `SET sql_safe_updates = 1`），没有 `WHERE`（或者 `WHERE` 没用到索引列）也没有 `LIMIT` 的 `UPDATE`、`DELETE` 会被拒绝：

  ```bash
  mysql --safe-updates shop -e "UPDATE product SET price = 0"
  ```

  ```
  ERROR 1175 (HY000): You are using safe update mode and you tried to update a table without a WHERE that uses a KEY column.
  ```

- 重要的数据要有备份（5.2 篇）。

## 6. 常见错误

- **用 `= NULL` 判断空值**：永远查不到，要用 `IS NULL`；
- **忘记 `NULL` 的三值逻辑**：`price < 100` 和 `NOT (price < 100)` 加起来不是全部，`price <> 299` 也不包括 `price` 为 `NULL` 的行；
- **`AND`、`OR` 混用不加括号**；
- **以为不写 `ORDER BY` 也有固定的顺序**；
- **`SELECT` 中出现非分组、非聚合的列**；
- **在 `WHERE` 中使用聚合函数或者 `SELECT` 中的别名**；
- **`UPDATE`、`DELETE` 忘写 `WHERE`**。

## 7. 小结

- `SELECT` 选择列和表达式，`WHERE` 过滤行，`ORDER BY` 排序，`LIMIT` 分页，`DISTINCT` 去重
- `AND` 比 `OR` 优先，混用时加括号
- `NULL` 表示未知，和它比较的结果是未知；判断用 `IS NULL`；聚合函数（`COUNT(*)` 除外）跳过 `NULL`
- `GROUP BY` 分组，`HAVING` 过滤组；分组后 `SELECT` 中只能有分组列和聚合函数
- 执行顺序：FROM → WHERE → GROUP BY → HAVING → SELECT → ORDER BY → LIMIT
- `INSERT` 写出列名；`ON DUPLICATE KEY UPDATE` 实现「存在就更新」；`UPDATE`、`DELETE` 先用 `SELECT` 确认范围，可以开启安全更新模式

## 8. 练习

（每题都在重新执行 `shop.sql` 后的原始数据上做。）

1. `SELECT COUNT(*) FROM product WHERE price <> 299;` 的结果是多少？怎样把未定价的商品也算进去？
2. 每页 4 件商品，按 `id` 排序，写出第 3 页的查询。结果有几行？
3. 统计每个分类中有货（库存大于 0）和缺货的商品数，只用一条查询。

<details>
<summary>点击查看答案</summary>

1. 结果是 8，不是 9：价格为 299 的机械键盘被排除，新品摄像头的 `NULL <> 299` 是未知，也被排除了。要把 `NULL` 也算进去，可以写 `WHERE price <> 299 OR price IS NULL`，或者用 MySQL 的 `<=>`（`NULL` 安全的等于，`NULL <=> NULL` 为真，`NULL <=> 299` 为假）：

   ```sql
   SELECT COUNT(*) FROM product WHERE NOT (price <=> 299);
   ```

   结果是 9。
2. 第 3 页跳过前两页的 8 行：

   ```sql
   SELECT id, name FROM product ORDER BY id LIMIT 4 OFFSET 8;
   ```

   ```
   +----+------------+
   | id | name       |
   +----+------------+
   |  9 | 蓝牙音箱   |
   | 10 | 新品摄像头 |
   +----+------------+
   ```

   只有 2 行，这是最后一页。总页数可以先用 `COUNT(*)` 算出：10 行，每页 4 行，共 3 页。
3. 在 MySQL 中，比较的结果是 1 或 0，可以直接求和：

   ```sql
   SELECT category, SUM(stock > 0) AS 有货, SUM(stock = 0) AS 缺货 FROM product GROUP BY category;
   ```

   ```
   +----------+------+------+
   | category | 有货 | 缺货 |
   +----------+------+------+
   | 外设     |    2 |    1 |
   | 显示     |    1 |    1 |
   | 配件     |    3 |    0 |
   | 音频     |    1 |    1 |
   +----------+------+------+
   ```

   在其他数据库中，可以写成标准 SQL 的 `SUM(CASE WHEN stock > 0 THEN 1 ELSE 0 END)`，或者 `COUNT(CASE WHEN stock > 0 THEN 1 END)`（没有 `ELSE` 时为 `NULL`，不被计数）。

</details>
