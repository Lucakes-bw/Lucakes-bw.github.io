---
title: 建表与数据类型
date: 2026-10-25 09:10:00
updated: 2026-10-25 09:10:00
categories:
  - 数据库
  - SQL基础
tags:
  - 数据库
  - MySQL
  - SQL
order: 2
description: 建表时为每一列选择类型和约束。本文逐一实测：10 个 0.1 相加，FLOAT 得到 1.0000000149011612，DOUBLE 得到 0.9999999999999999，只有 DECIMAL 等于 1；utf8mb3 存不下一个表情符号；客户端字符集设错时，「中文」存进去变成了 6 个字符，换一个客户端就成了乱码；TIMESTAMP 到 2038 年就存不下了；外键让不存在的客户无法下单；关掉严格模式后，1000 被悄悄存成了 127。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会为列选择合适的类型：整数、定点数和浮点数、字符串、日期时间；理解字符集和排序规则，会避免乱码；会使用主键、自增、非空、默认值、唯一、检查、外键等约束；会用 `ALTER TABLE` 修改表；理解严格模式。
>
> **前置知识**：上一篇《数据库与 MySQL 入门》。了解整数的范围和浮点数的误差（C 语言专栏《数据类型》）。
>
> **实验环境**：MySQL 8.4.13（Docker），客户端字符集 utf8mb4（第 4.4 节会说明为什么要强调这一点）。本文的表都建在一个实验用的库 `lab` 中。

## 1. 建表的语法

```sql
CREATE TABLE 表名 (
    列名 类型 [约束],
    列名 类型 [约束],
    ...
    [表级约束]
);
```

每一列至少要有名字和类型。**类型**决定了这一列能存什么值、占多少空间、怎样比较和排序；**约束**进一步限制值（不能为空、不能重复……）。选错类型的后果往往要等到数据多了以后才暴露：金额对不上、表情符号存不进去、2038 年时间溢出。下面逐类来看，然后看约束。

## 2. 整数

| 类型 | 字节 | 有符号范围 | 无符号范围（`UNSIGNED`） |
|---|---|---|---|
| `TINYINT` | 1 | -128 ～ 127 | 0 ～ 255 |
| `SMALLINT` | 2 | -32768 ～ 32767 | 0 ～ 65535 |
| `MEDIUMINT` | 3 | 约 ±838 万 | 0 ～ 约 1677 万 |
| `INT` | 4 | 约 ±21 亿 | 0 ～ 约 42 亿 |
| `BIGINT` | 8 | 约 ±922 亿亿 | 0 ～ 约 1845 亿亿 |

和 C 语言的整数类型一样，只是名字不同。超出范围会怎样？

```sql
CREATE TABLE t_int (a TINYINT, b TINYINT UNSIGNED, c INT, d BIGINT);
INSERT INTO t_int VALUES (127, 255, 2147483647, 9223372036854775807);
INSERT INTO t_int VALUES (128, 0, 0, 0);
INSERT INTO t_int VALUES (0, -1, 0, 0);
```

```
Query OK, 1 row affected (0.00 sec)

ERROR 1264 (22003): Out of range value for column 'a' at row 1
ERROR 1264 (22003): Out of range value for column 'b' at row 1
```

每种类型的最大值都存进去了；128 超出了 `TINYINT`，-1 超出了 `TINYINT UNSIGNED`，都被拒绝。和 C 语言不同，MySQL 不会让它「回绕」成 -128 或 255，而是直接报错（这依赖于严格模式，见第 7 节）。

选择时按「可能的最大值」留足余量：年龄用 `TINYINT UNSIGNED`，一般的编号用 `INT UNSIGNED`（约 42 亿），数据量可能非常大的表（日志、订单）的编号用 `BIGINT`。

> 老版本的 MySQL 中常见 `INT(11)` 这样的写法，括号中的数字是「显示宽度」，和能存多大的数没有关系。MySQL 8.0 起已经不推荐使用。

## 3. 小数：DECIMAL 和 FLOAT、DOUBLE

`FLOAT`（4 字节）和 `DOUBLE`（8 字节）是二进制浮点数，和 C 语言的 `float`、`double` 一样，大多数十进制小数（例如 0.1）不能精确表示。`DECIMAL(M, D)` 是**定点数**：共 M 位数字，其中 D 位是小数，按十进制精确存储。

同一个值 0.1 存 10 行，分别求和：

```sql
CREATE TABLE t_num (f FLOAT, d DOUBLE, m DECIMAL(10, 2));
INSERT INTO t_num VALUES (0.1, 0.1, 0.1), (0.1, 0.1, 0.1), ...;   -- 共 10 行
SELECT SUM(f), SUM(d), SUM(m) FROM t_num;
SELECT SUM(f) = 1, SUM(d) = 1, SUM(m) = 1 FROM t_num;
```

```
+--------------------+--------------------+--------+
| SUM(f)             | SUM(d)             | SUM(m) |
+--------------------+--------------------+--------+
| 1.0000000149011612 | 0.9999999999999999 |   1.00 |
+--------------------+--------------------+--------+

+------------+------------+------------+
| SUM(f) = 1 | SUM(d) = 1 | SUM(m) = 1 |
+------------+------------+------------+
|          0 |          0 |          1 |
+------------+------------+------------+
```

`FLOAT` 多了一点，`DOUBLE` 少了一点，都不等于 1；只有 `DECIMAL` 精确等于 1。所以**金额必须用 `DECIMAL`**，否则账目一定会对不上。`FLOAT`、`DOUBLE` 适合科学测量这类本来就有误差、更看重范围的数据。

SQL 中直接写的小数常量本身就是精确的定点数，`e` 表示法的才是浮点数：

```sql
SELECT 0.1 + 0.2 = 0.3, 0.1e0 + 0.2e0 = 0.3e0;
```

```
+-----------------+-----------------------+
| 0.1 + 0.2 = 0.3 | 0.1e0 + 0.2e0 = 0.3e0 |
+-----------------+-----------------------+
|               1 |                     0 |
+-----------------+-----------------------+
```

`DECIMAL(10, 2)` 小数位数多了会四舍五入，整数部分超出范围会报错：

```sql
INSERT INTO t_num VALUES (0, 0, 9.999);
SHOW WARNINGS;
INSERT INTO t_num VALUES (0, 0, 123456789.1);
```

```
Query OK, 1 row affected, 1 warning (0.01 sec)

+-------+------+----------------------------------------+
| Level | Code | Message                                |
+-------+------+----------------------------------------+
| Note  | 1265 | Data truncated for column 'm' at row 1 |
+-------+------+----------------------------------------+

ERROR 1264 (22003): Out of range value for column 'm' at row 1
```

9.999 存成了 10.00，只给了一个 `Note`（提示），没有报错。`DECIMAL(10, 2)` 的整数部分最多 8 位，123456789 有 9 位，被拒绝。

**`1 warning` 要看一看。** 客户端只显示有几个警告，不显示内容，要用 `SHOW WARNINGS` 查看。很多数据问题就藏在被忽略的警告中。

## 4. 字符串

### 4.1 CHAR 和 VARCHAR

| 类型 | 说明 |
|---|---|
| `CHAR(n)` | 定长，最多 n 个**字符**，不足时用空格补齐，读出时去掉末尾的空格 |
| `VARCHAR(n)` | 变长，最多 n 个**字符**，实际占用「内容的字节数 + 1～2 字节长度」 |
| `TEXT`、`MEDIUMTEXT`、`LONGTEXT` | 长文本，最多约 64 KB、16 MB、4 GB |
| `BLOB` 系列 | 二进制数据，不涉及字符集 |

注意 n 是**字符数**，不是字节数：

```sql
CREATE TABLE t_str (c CHAR(5), v VARCHAR(5));
INSERT INTO t_str VALUES ('你好世界啊', '你好世界啊');
INSERT INTO t_str VALUES ('你好世界啊!', 'x');
SELECT c, v, LENGTH(v), CHAR_LENGTH(v) FROM t_str;
```

```
Query OK, 1 row affected (0.00 sec)

ERROR 1406 (22001): Data too long for column 'c' at row 1

+------------+------------+-----------+----------------+
| c          | v          | LENGTH(v) | CHAR_LENGTH(v) |
+------------+------------+-----------+----------------+
| 你好世界啊 | 你好世界啊 |        15 |              5 |
+------------+------------+-----------+----------------+
```

5 个汉字正好存进 `CHAR(5)` 和 `VARCHAR(5)`，第 6 个字符就超长了。`LENGTH` 返回**字节数**（每个汉字 3 字节，共 15），`CHAR_LENGTH` 返回**字符数**。

`CHAR` 会去掉末尾的空格，`VARCHAR` 原样保留：

```sql
INSERT INTO t_str VALUES ('ab  ', 'ab  ');
SELECT CONCAT('[', c, ']'), CONCAT('[', v, ']') FROM t_str WHERE c = 'ab';
```

```
+---------------------+---------------------+
| CONCAT('[', c, ']') | CONCAT('[', v, ']') |
+---------------------+---------------------+
| [ab]                | [ab  ]              |
+---------------------+---------------------+
```

一般的字符串都用 `VARCHAR`；长度固定的值（如 32 位的 MD5 值、国家代码）可以用 `CHAR`。`VARCHAR(n)` 的 n 按业务上合理的最大长度定，不要随手写 `VARCHAR(255)`：n 会影响内存中临时表和排序时分配的空间。

### 4.2 字符集：utf8mb4，而不是 utf8

**字符集**规定了字符怎样编码成字节。在 MySQL 中只需要记住一点：**用 utf8mb4**。

UTF-8 编码中，一个字符占 1～4 个字节：

```sql
SELECT LENGTH('a'), LENGTH('é'), LENGTH('中'), LENGTH('😀') AS emoji, CHAR_LENGTH('😀') AS emoji_chars;
```

```
+-------------+-------------+--------------+-------+-------------+
| LENGTH('a') | LENGTH('é') | LENGTH('中') | emoji | emoji_chars |
+-------------+-------------+--------------+-------+-------------+
|           1 |           2 |            3 |     4 |           1 |
+-------------+-------------+--------------+-------+-------------+
```

MySQL 早期实现的 `utf8` 字符集每个字符最多只用 3 个字节，存不下 4 字节的字符（表情符号、一些生僻字）。它现在的正式名字是 `utf8mb3`，后来补上的完整 UTF-8 叫 `utf8mb4`（most bytes 4）：

```sql
CREATE TABLE t_mb3 (s VARCHAR(10)) CHARACTER SET utf8mb3;
INSERT INTO t_mb3 VALUES ('中文');
INSERT INTO t_mb3 VALUES ('笑😀');
CREATE TABLE t_mb4 (s VARCHAR(10)) CHARACTER SET utf8mb4;
INSERT INTO t_mb4 VALUES ('笑😀');
SELECT s, LENGTH(s), CHAR_LENGTH(s) FROM t_mb4;
```

```
Query OK, 0 rows affected, 1 warning (0.00 sec)
Query OK, 1 row affected (0.00 sec)
ERROR 1366 (HY000): Incorrect string value: '\xF0\x9F\x98\x80' for column 's' at row 1
Query OK, 0 rows affected (0.00 sec)
Query OK, 1 row affected (0.00 sec)

+------+-----------+----------------+
| s    | LENGTH(s) | CHAR_LENGTH(s) |
+------+-----------+----------------+
| 笑😀 |         7 |              2 |
+------+-----------+----------------+
```

建 `utf8mb3` 的表时就有一个警告：`'utf8mb3' is deprecated ... Please use utf8mb4 instead`。中文能存，表情符号 `F0 9F 98 80` 被拒绝。MySQL 8.0 起默认字符集已经是 `utf8mb4`，但在老系统和从老系统迁移来的表中仍然经常遇到 `utf8`。

### 4.3 排序规则

**排序规则**（collation）决定了字符串怎样比较和排序。MySQL 8.0 的默认排序规则是 `utf8mb4_0900_ai_ci`：`ai` 是 accent insensitive（不区分重音），`ci` 是 case insensitive（不区分大小写）：

```sql
SELECT 'abc' = 'ABC', 'a' = 'á', 'abc' = 'abc  ';
```

```
+---------------+-----------+-----------------+
| 'abc' = 'ABC' | 'a' = 'á' | 'abc' = 'abc  ' |
+---------------+-----------+-----------------+
|             1 |         1 |               0 |
+---------------+-----------+-----------------+
```

不区分大小写通常是想要的（用户名 `Alice` 和 `alice` 视为同一个），但有时不是（区分大小写的验证码、文件名）。这时可以给列指定 `utf8mb4_bin`（按字节比较）或 `utf8mb4_0900_as_cs`（区分重音和大小写）。

### 4.4 乱码是怎样产生的

数据库中有三个地方涉及字符集：**列**的字符集，**连接**的字符集（客户端告诉服务器「我发来的字节是什么编码」），客户端**终端**实际使用的编码。三者不一致，就会出现乱码。

上一篇用的 MySQL 镜像中没有设置语言环境，客户端会默认使用 `latin1` 作为连接字符集。重现一下：终端发出的是 UTF-8 编码的「中文」（6 个字节），客户端却声称它们是 latin1：

```bash
mysql --default-character-set=latin1 lab -e "INSERT INTO gbl VALUES ('中文')"
```

服务器把这 6 个字节当作 6 个 latin1 字符，再转换成 utf8mb4 存入列中（`gbl` 是一个只有一列 `s VARCHAR(20)` 的 utf8mb4 表）。用同一个 latin1 客户端执行 `SELECT s, CHAR_LENGTH(s), HEX(s) FROM gbl`，看起来一切正常：

```
中文	6	C3A4C2B8C2ADC3A6E28093E280A1
```

但 `CHAR_LENGTH` 是 6，不是 2；存储的字节 `C3 A4 C2 B8 …` 也不是「中」的 UTF-8 编码 `E4 B8 AD`。读的时候服务器又按 latin1 转换回去，错误恰好被抵消了。换一个正确设置了 utf8mb4 的客户端（或者任何应用程序）读：

```
ä¸­æ–‡	6	C3A4C2B8C2ADC3A6E28093E280A1
```

乱码出现了。`WHERE s = '中文'` 也查不到这一行。这种「双重编码」的数据最麻烦：写入时一切正常，等发现时已经存了大量错误的数据。

所以连接字符集必须和实际的编码一致：命令行客户端设置 `--default-character-set=utf8mb4`（或者设置 `LANG=C.UTF-8` 这样的语言环境），程序中在连接参数里指定 `utf8mb4`。可以用 `SELECT @@character_set_client` 检查当前连接的设置。

## 5. 日期和时间

| 类型 | 格式 | 范围 | 说明 |
|---|---|---|---|
| `DATE` | `2026-10-25` | 1000 ～ 9999 年 | 日期 |
| `TIME` | `10:00:00` | ±838 小时 | 时间或时长 |
| `DATETIME` | `2026-10-25 10:00:00` | 1000 ～ 9999 年 | 存什么就是什么 |
| `TIMESTAMP` | `2026-10-25 10:00:00` | 1970 ～ 2038 年 | 内部存 UTC 秒数，按会话时区显示 |

`DATETIME` 和 `TIMESTAMP` 看起来一样，区别在于时区。服务器在东八区（`@@system_time_zone` 是 CST），同一个时刻存入两列，再把会话的时区改成 UTC：

```sql
CREATE TABLE t_time (dt DATETIME, ts TIMESTAMP);
INSERT INTO t_time VALUES ('2026-10-25 10:00:00', '2026-10-25 10:00:00');
SET time_zone = '+00:00';
SELECT * FROM t_time;
```

```
+---------------------+---------------------+
| dt                  | ts                  |
+---------------------+---------------------+
| 2026-10-25 10:00:00 | 2026-10-25 02:00:00 |
+---------------------+---------------------+
```

`TIMESTAMP` 存入时按东八区转换成 UTC，读出时按 UTC 显示，成了 02:00；`DATETIME` 不做任何转换。如果用户分布在多个时区，`TIMESTAMP` 能自动换算；`DATETIME` 则要由应用程序约定存的是哪个时区的时间（通常统一存 UTC）。

`TIMESTAMP` 内部是 4 字节的秒数，和 C 语言中 32 位的 `time_t` 一样，在 UTC 2038-01-19 03:14:07（东八区 11:14:07）之后溢出：

```sql
SET time_zone = 'SYSTEM';
INSERT INTO t_time VALUES ('2038-01-19 11:14:07', '2038-01-19 11:14:07');
INSERT INTO t_time VALUES ('2038-01-19 11:14:08', '2038-01-19 11:14:08');
INSERT INTO t_time VALUES ('9999-12-31 23:59:59', NULL);
```

```
Query OK, 1 row affected (0.00 sec)
ERROR 1292 (22007): Incorrect datetime value: '2038-01-19 11:14:08' for column 'ts' at row 1
Query OK, 1 row affected (0.00 sec)
```

晚 1 秒就存不进去了，`DATETIME` 可以存到 9999 年。会员到期时间、贷款到期日这样的值很容易超过 2038 年，应该用 `DATETIME`。

不存在的日期会被拒绝：

```sql
INSERT INTO t_time VALUES ('2026-02-30 00:00:00', NULL);
```

```
ERROR 1292 (22007): Incorrect datetime value: '2026-02-30 00:00:00' for column 'dt' at row 1
```

## 6. 约束

用一个客户表把常用的约束都用上：

```sql
CREATE TABLE customer (
    id         INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    email      VARCHAR(100) NOT NULL UNIQUE,
    name       VARCHAR(50)  NOT NULL,
    age        TINYINT UNSIGNED CHECK (age BETWEEN 1 AND 150),
    vip        BOOLEAN NOT NULL DEFAULT FALSE,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
);
```

| 约束 | 作用 |
|---|---|
| `PRIMARY KEY` | 主键：唯一且非空，每个表只能有一个（可以由多列组成） |
| `AUTO_INCREMENT` | 插入时不指定值，自动取「当前最大值 + 1」 |
| `NOT NULL` | 不能为空 |
| `UNIQUE` | 不能重复（但可以有多个 `NULL`） |
| `CHECK` | 值必须满足条件（MySQL 8.0.16 起才真正检查） |
| `DEFAULT` | 插入时不指定，就用默认值 |
| `FOREIGN KEY` | 值必须在另一个表中存在 |

`BOOLEAN` 其实是 `TINYINT(1)` 的别名，`FALSE` 就是 0。逐一试试：

```sql
INSERT INTO customer (email, name, age) VALUES ('li@example.com', '李雷', 20);
INSERT INTO customer (email, name) VALUES ('han@example.com', '韩梅梅');
INSERT INTO customer (email, name) VALUES ('li@example.com', '李四');
INSERT INTO customer (email) VALUES ('wang@example.com');
INSERT INTO customer (email, name, age) VALUES ('old@example.com', '老人', 200);
INSERT INTO customer (email, name) VALUES ('zhang@example.com', '张三');
SELECT * FROM customer;
```

```
Query OK, 1 row affected (0.00 sec)
Query OK, 1 row affected (0.00 sec)
ERROR 1062 (23000): Duplicate entry 'li@example.com' for key 'customer.email'
ERROR 1364 (HY000): Field 'name' doesn't have a default value
ERROR 3819 (HY000): Check constraint 'customer_chk_1' is violated.
Query OK, 1 row affected (0.00 sec)

+----+-------------------+--------+------+-----+---------------------+
| id | email             | name   | age  | vip | created_at          |
+----+-------------------+--------+------+-----+---------------------+
|  1 | li@example.com    | 李雷   |   20 |   0 | 2026-10-25 10:19:35 |
|  2 | han@example.com   | 韩梅梅 | NULL |   0 | 2026-10-25 10:19:35 |
|  4 | zhang@example.com | 张三   | NULL |   0 | 2026-10-25 10:19:35 |
+----+-------------------+--------+------+-----+---------------------+
```

- 重复的邮箱、没有姓名、年龄 200，都被拒绝了；
- 插入时没有指定 `id`、`vip`、`created_at`，它们分别由自增、默认值、当前时间填上；
- 没有指定 `age`，它没有默认值也允许为空，于是是 `NULL`；
- 张三的 `id` 是 4，不是 3。3 去哪了？见练习 1。

### 6.1 外键

订单属于某个客户，订单表的 `customer_id` 引用客户表的 `id`：

```sql
CREATE TABLE orders (
    id          INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    customer_id INT UNSIGNED NOT NULL,
    amount      DECIMAL(10, 2) NOT NULL,
    FOREIGN KEY (customer_id) REFERENCES customer (id)
);
INSERT INTO orders (customer_id, amount) VALUES (1, 299.00);
INSERT INTO orders (customer_id, amount) VALUES (99, 10.00);
DELETE FROM customer WHERE id = 1;
DELETE FROM customer WHERE id = 2;
```

```
Query OK, 1 row affected (0.00 sec)
ERROR 1452 (23000): Cannot add or update a child row: a foreign key constraint fails (`lab`.`orders`, CONSTRAINT `orders_ibfk_1` FOREIGN KEY (`customer_id`) REFERENCES `customer` (`id`))
ERROR 1451 (23000): Cannot delete or update a parent row: a foreign key constraint fails (`lab`.`orders`, CONSTRAINT `orders_ibfk_1` FOREIGN KEY (`customer_id`) REFERENCES `customer` (`id`))
Query OK, 1 row affected (0.00 sec)
```

外键从两个方向保证数据的一致：不能给不存在的客户（99）下订单；客户 1 有订单，不能删除；客户 2 没有订单，可以删除。

外键的代价是每次插入、删除都要到另一个表中检查，在高并发的系统中还会带来额外的锁。所以很多互联网公司的规范是**不用外键**，由应用程序保证一致性。学习和中小型系统中，建议使用外键，让数据库替你把关。

## 7. 严格模式

上面的超出范围、超长、非法日期都报了错，是因为 `sql_mode` 中包含 `STRICT_TRANS_TABLES`（严格模式）：

```sql
SELECT @@sql_mode;
```

```
ONLY_FULL_GROUP_BY,STRICT_TRANS_TABLES,NO_ZERO_IN_DATE,NO_ZERO_DATE,ERROR_FOR_DIVISION_BY_ZERO,NO_ENGINE_SUBSTITUTION
```

把它关掉，再插入超出范围和超长的值：

```sql
SET SESSION sql_mode = '';
INSERT INTO t_int VALUES (1000, -5, 0, 0);
INSERT INTO t_str VALUES ('abcdefgh', '一二三四五六七');
SELECT * FROM t_int;
SELECT * FROM t_str;
```

```
Query OK, 1 row affected, 2 warnings (0.00 sec)
Query OK, 1 row affected, 2 warnings (0.00 sec)

+-----+-----+------------+---------------------+
| a   | b   | c          | d                   |
+-----+-----+------------+---------------------+
| 127 | 255 | 2147483647 | 9223372036854775807 |
| 127 |   0 |          0 |                   0 |
+-----+-----+------------+---------------------+

+------------+------------+
| c          | v          |
+------------+------------+
| 你好世界啊 | 你好世界啊 |
| ab         | ab         |
| abcde      | 一二三四五 |
+------------+------------+
```

插入「成功」了，只有两个警告：1000 被存成了 127（`TINYINT` 的最大值），-5 被存成了 0，字符串被截断。程序以为数据存好了，实际上数据已经错了。MySQL 5.7 之前的默认设置就是这样，很多老系统中的脏数据就是这样来的。**不要关闭严格模式。**

## 8. 修改和删除表

```sql
ALTER TABLE customer ADD COLUMN phone VARCHAR(20) AFTER name;   -- 增加一列，放在 name 后面
ALTER TABLE customer MODIFY name VARCHAR(100) NOT NULL;         -- 修改类型
ALTER TABLE customer RENAME COLUMN vip TO is_vip;               -- 改列名
SHOW CREATE TABLE customer\G
```

```
Create Table: CREATE TABLE `customer` (
  `id` int unsigned NOT NULL AUTO_INCREMENT,
  `email` varchar(100) NOT NULL,
  `name` varchar(100) NOT NULL,
  `phone` varchar(20) DEFAULT NULL,
  `age` tinyint unsigned DEFAULT NULL,
  `is_vip` tinyint(1) NOT NULL DEFAULT '0',
  `created_at` datetime NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`id`),
  UNIQUE KEY `email` (`email`),
  CONSTRAINT `customer_chk_1` CHECK ((`age` between 1 and 150))
) ENGINE=InnoDB AUTO_INCREMENT=5 DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci
```

语句末尾的 `\G` 代替分号，让结果竖着显示，适合很长的结果。`SHOW CREATE TABLE` 显示了表的完整定义，包括 MySQL 自动起的约束名（`customer_chk_1`）、存储引擎 `InnoDB`、下一个自增值 5、字符集和排序规则。

注意 `ADD COLUMN` 显示 `0 rows affected`，而 `MODIFY` 显示 `2 rows affected`：增加一列只改了表的定义，而修改类型要把每一行都重写一遍。表有上亿行时，这样的 `ALTER` 可能要执行几个小时，在线上执行之前要先评估。

删除表用 `DROP TABLE 表名`，清空表中所有的行而保留表用 `TRUNCATE TABLE 表名`。**它们都无法撤销。**

## 9. 常见错误

- **金额用 `FLOAT` 或 `DOUBLE`**：10 个 0.1 加起来不等于 1；
- **用 `utf8` 而不是 `utf8mb4`**：表情符号存不进去；
- **连接字符集和实际编码不一致**：数据被双重编码，换一个客户端就成了乱码；
- **该用 `DATETIME` 的地方用了 `TIMESTAMP`**：2038 年以后的时间存不进去；
- **手机号、身份证号用整数**：手机号超出 `INT` 的范围，开头的 0 也会丢失（练习 2）。不参与计算的「号码」都应该用字符串；
- **忽略警告**：`1 warning` 中可能是被截断或修改的数据；
- **关闭严格模式**：错误的数据被悄悄存进去。

## 10. 小结

- 整数按最大值选择类型，超出范围在严格模式下报错
- 金额用 `DECIMAL`；`FLOAT`、`DOUBLE` 有误差，实测 10 个 0.1 相加都不等于 1
- `VARCHAR(n)` 的 n 是字符数；字符集用 `utf8mb4`，`utf8mb3` 存不下表情符号；默认排序规则不区分大小写和重音
- 连接字符集必须和实际编码一致，否则数据被双重编码
- `TIMESTAMP` 按时区转换，只能到 2038 年；`DATETIME` 不转换，可以到 9999 年
- 约束让数据库替你把关：主键、自增、非空、默认值、唯一、检查、外键
- 保持严格模式，关掉它后 1000 被悄悄存成了 127

## 11. 练习

1. 第 6 节中张三的 `id` 为什么是 4？三条失败的插入中，是哪一条「用掉」了 3？
2. 手机号 `13800138000` 用 `INT`、`BIGINT`、`VARCHAR(20)` 存，分别会怎样？如果号码以 0 开头呢（例如带区号的固定电话 `01088886666`）？
3. 客户 1 有订单，无法删除。如果业务上要求「删除客户时，同时删除他的所有订单」，怎样定义外键？

<details>
<summary>点击查看答案</summary>

1. 用一个新表分别试验三种失败（每次失败后查看自增的下一个值）：

   ```
   INSERT INTO ai (u) VALUES ("y")                 -- 违反 NOT NULL
   ERROR 1364 (HY000): Field 'n' doesn't have a default value
   下一个自增值：2
   INSERT INTO ai (u, n, a) VALUES ("z", "z", -1)  -- 违反 CHECK
   ERROR 3819 (HY000): Check constraint 'ai_chk_1' is violated.
   下一个自增值：2
   INSERT INTO ai (u, n) VALUES ("x", "dup")       -- 违反 UNIQUE
   ERROR 1062 (23000): Duplicate entry 'x' for key 'ai.u'
   下一个自增值：3
   ```

   违反非空和检查约束时，在分配自增值之前就失败了；违反唯一约束时，自增值已经分配了（要真正插入时才发现重复），失败后**不会退回**。所以是「李四」那条重复邮箱的插入用掉了 3。事务回滚时自增值也不会退回。因此自增的 `id` 只保证唯一和递增，**不保证连续**，不要用它来统计行数，也不要假设 `id` 没有空缺。
2. 实测：

   ```
   INSERT INTO p VALUES (13800138000, NULL, NULL);
   ERROR 1264 (22003): Out of range value for column 'phone' at row 1
   INSERT INTO p VALUES (NULL, 013800138000, '013800138000');
   ```

   ```
   +-------+-------------+--------------+
   | phone | phone2      | phone3       |
   +-------+-------------+--------------+
   |  NULL | 13800138000 | 013800138000 |
   +-------+-------------+--------------+
   ```

   `INT` 最大约 21 亿，存不下 11 位的手机号；`BIGINT` 存得下，但开头的 0 丢了；只有字符串原样保存。号码不需要做加减乘除，还可能有 `+86`、`-` 等字符，应该用 `VARCHAR`。
3. 定义外键时加上 `ON DELETE CASCADE`：

   ```sql
   FOREIGN KEY (customer_id) REFERENCES customer (id) ON DELETE CASCADE
   ```

   实测：客户 4 有两笔订单，客户 1 有一笔，删除客户 4 后，订单表中只剩下客户 1 的那一笔，客户 4 的两笔被自动删除了。其他选项还有 `ON DELETE SET NULL`（把订单的 `customer_id` 置为 `NULL`，要求这一列允许为空）和默认的 `RESTRICT`（拒绝删除）。级联删除很方便，也很危险：一条 `DELETE` 可能连带删除大量的数据，使用前要想清楚。

</details>
