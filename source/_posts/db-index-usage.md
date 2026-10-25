---
title: 联合索引与索引失效
date: 2026-10-25 10:30:00
updated: 2026-10-25 10:30:00
categories:
  - 数据库
  - 索引与查询优化
tags:
  - 数据库
  - MySQL
  - 索引
  - 性能
order: 4
description: 什么样的条件能用上索引？本文在 100 万行的用户表上建一个 (city, age, name) 的联合索引，用 EXPLAIN 的 key_len 看每条查询用到了索引的哪几列：只有 age 的条件用不上它，age 用了范围条件，name 就只能逐条过滤，同一条查询改成 IN 列表后从约 0.9 毫秒降到约 0.06 毫秒。然后逐一实测让索引失效的写法：在列上用函数、给字符串列传数字、以 % 开头的 LIKE，都从 0.03 毫秒变成了 90 毫秒左右；函数索引又把它降了回来。最后，只有两种取值的 gender 列上的索引骗过了优化器，比全表扫描慢了近 3 倍。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解联合索引的排序方式和最左前缀原则；会用 `key_len` 判断用到了索引的哪几列；理解范围条件对后面的列的影响、索引条件下推和跳跃扫描；知道哪些写法会让索引失效，以及怎样改写；会根据查询设计索引。
>
> **前置知识**：本章前三篇，特别是上一篇的 `EXPLAIN` 和计时方法。
>
> **实验环境**：MySQL 8.4.13（Docker），缓冲池 128 MB。耗时是服务器端的执行时间，每条查询连续执行 3 次。

## 1. 测试表

100 万个用户，城市有 20 种，年龄 18～80 岁：

```sql
CREATE TABLE u (
    id         INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    city       VARCHAR(10) NOT NULL,
    age        TINYINT UNSIGNED NOT NULL,
    name       VARCHAR(20) NOT NULL,
    gender     CHAR(1) NOT NULL,
    phone      VARCHAR(11) NOT NULL,
    created_at DATETIME NOT NULL
);
-- 用 WITH RECURSIVE 生成 100 万行，方法同《实战：设计一个博客数据库》练习 2
CREATE INDEX idx_city_age_name ON u (city, age, name);
CREATE INDEX idx_phone ON u (phone);
CREATE INDEX idx_created ON u (created_at);
CREATE INDEX idx_gender ON u (gender);
```

每个城市 5 万人，北京 30 岁的有 794 人。

## 2. 联合索引

`idx_city_age_name` 是建在三列上的**联合索引**。它的记录先按 `city` 排序，`city` 相同的再按 `age` 排序，`age` 也相同的再按 `name` 排序，最后是主键 `id`。按索引的顺序取出其中的一段：

```
+------+-----+-------------+--------+
| city | age | name        | id     |
+------+-----+-------------+--------+
| ...  |     |             |        |
| 上海 |  80 | name0998416 | 692261 |
| 上海 |  80 | name0999203 |  64781 |
| 北京 |  18 | name0000935 | 855540 |
| 北京 |  18 | name0001722 | 228060 |
| 北京 |  18 | name0003444 | 456120 |
| ...  |     |             |        |
| 北京 |  19 | name0000350 | 534160 |
| 北京 |  19 | name0002072 | 762220 |
| ...  |     |             |        |
+------+-----+-------------+--------+
```

（「上海」排在「北京」前面，是因为默认的排序规则按 Unicode 编码比较汉字，上 U+4E0A 小于北 U+5317。）就像字典先按第一个字母排，再按第二个字母排。理解了这个排序方式，联合索引的所有规则都能推出来。

### 2.1 最左前缀

```
type   key                key_len  rows   Extra
ref    idx_city_age_name  42       98082  NULL         ← WHERE city = '北京'
ref    idx_city_age_name  43         794  NULL         ← WHERE city = '北京' AND age = 30
ref    idx_city_age_name  125          1  NULL         ← WHERE city = '北京' AND age = 75 AND name = 'name0000853'
ref    idx_city_age_name  43         794  NULL         ← WHERE age = 30 AND city = '北京'
ALL    NULL               NULL    996300  Using where  ← WHERE age = 30
ALL    NULL               NULL    996300  Using where  ← WHERE age = 30 AND name = 'name0500000'
```

（都是 `SELECT * FROM u WHERE …`，只列出了 `EXPLAIN` 中关心的几列。）`key_len` 说明用到了索引的前几列：

- `city` 是 `VARCHAR(10)`，utf8mb4 最多 40 字节，加 2 字节长度，共 42；
- 加上 `age`（`TINYINT`，1 字节），43；
- 再加上 `name`（`VARCHAR(20)`，80 + 2），125。

条件中有 `city`，就能从索引中找到「北京」那一段；再有 `age`，就能在那一段中进一步找到「30 岁」的一小段。但只有 `age = 30` 时，30 岁的人分散在每个城市的那一段中间（上海 30 岁、北京 30 岁、广州 30 岁……），索引的第一列没有条件，就无法定位，只能扫描全表。这就是**最左前缀原则**：联合索引只能从最左边的列开始，连续地使用。

条件的书写顺序没有关系，`age = 30 AND city = '北京'` 也能用上前两列，优化器会自己调整。

### 2.2 范围条件之后的列

```
type   key                key_len  rows   Extra
ref    idx_city_age_name  42       98082  Using index condition  ← WHERE city = '北京' AND name = 'name0000853'
range  idx_city_age_name  43       14920  Using index condition  ← WHERE city = '北京' AND age > 70 AND name = 'name0000853'
range  idx_city_age_name  125         10  Using index condition  ← WHERE city = '北京' AND age IN (71, 72, …, 80) AND name = 'name0000853'
```

- 跳过了中间的 `age`：只能用 `city` 定位（`key_len` 42），`name` 不能用来定位。在「北京」那一段中，`name` 并不是有序的（它只在 `age` 相同时才有序）；
- `age > 70` 是范围条件：可以用 `city` 和 `age` 定位到「北京、71 岁到 80 岁」这一段（`key_len` 43），但在这一段中，`name` 同样不是有序的（71 岁的 name 排完了，72 岁的又从头开始），所以 `name` 不能用来定位。**联合索引中，范围条件之后的列不能再用来定位**；
- 把 `age > 70` 改成等价的 `IN` 列表：`IN` 相当于 10 个等值条件，每个年龄都能用上 `name`，`key_len` 是 125，估计只有 10 行。

计时：

```
毫秒：0.1 0.05 0.04       ← WHERE city = '北京' AND age = 75 AND name = 'name0000853'
毫秒：6.36 5.36 5.18      ← WHERE city = '北京' AND name = 'name0000853'
毫秒：0.89 0.83 0.98      ← WHERE city = '北京' AND age > 70 AND name = 'name0000853'
毫秒：0.09 0.06 0.06      ← WHERE city = '北京' AND age IN (71, 72, …, 80) AND name = 'name0000853'
```

三列都用上时最快；只用上 `city` 时，要在北京的 5 万条记录中逐条检查 `name`；`age > 70` 时，要检查 71～80 岁的约 7900 条（`EXPLAIN` 估计的 14920 偏多）；改成 `IN` 后又回到了接近等值查询的速度。所以联合索引中，**等值条件的列放在前面，范围条件的列放在后面**。

### 2.3 索引条件下推

上面的 `Extra` 都是 `Using index condition`，这是**索引条件下推**（Index Condition Pushdown，ICP）。`name` 虽然不能用来定位，但它就在索引的记录中，存储引擎扫描索引时可以先用 `name` 的条件过滤，只对满足条件的记录回表。没有 ICP 时，北京的 5 万条记录每一条都要先回表取出整行，再由服务器层判断 `name`。

### 2.4 跳跃扫描

只有 `age = 30` 的条件时，`SELECT *` 是全表扫描。但如果只查索引中有的列：

```
type   key                key_len  rows   Extra
range  idx_city_age_name  43       99630  Using where; Using index for skip scan  ← SELECT city, age, name FROM u WHERE age = 30
```

```
毫秒：4.25 3.64 3.36      ← SELECT city, age, name FROM u WHERE age = 30
```

这是 MySQL 8.0.13 起支持的**跳跃扫描**（skip scan）：第一列 `city` 只有 20 种取值，就把它拆成 20 次查找，「上海、30 岁」「北京、30 岁」……每次都能用前两列定位。它要求第一列的取值很少，并且查询只用到索引中的列（覆盖索引）。不要依赖它来设计索引，它只是最左前缀原则的一个补充。

### 2.5 排序

联合索引同样可以用于排序，只要排序的列在索引中是有序的：

```
type   key                key_len  rows   Extra
ref    idx_city_age_name  42       98082  NULL                 ← WHERE city = '北京' ORDER BY age LIMIT 10
ref    idx_city_age_name  42       98082  Using filesort       ← WHERE city = '北京' ORDER BY name LIMIT 10
ref    idx_city_age_name  43         794  NULL                 ← WHERE city = '北京' AND age = 30 ORDER BY name LIMIT 10
ALL    NULL               NULL    996300  Using filesort       ← ORDER BY age, city LIMIT 10
```

```
毫秒：0.54 0.37 0.34      ← WHERE city = '北京' ORDER BY age LIMIT 10
毫秒：32.77 30.34 29.99   ← WHERE city = '北京' ORDER BY name LIMIT 10
```

- `city` 固定以后，`age` 是有序的，直接按索引读出前 10 条；
- `city` 固定以后，`name` 不是有序的，要把北京的 5 万行全部取出来排序；
- `city` 和 `age` 都固定以后，`name` 又是有序的了；
- `ORDER BY age, city` 和索引的顺序（先 `city` 后 `age`）不一致，用不上。

规则和最左前缀相同：`WHERE` 中等值条件的列，加上 `ORDER BY` 的列，要能组成索引的一个前缀，并且顺序一致。

## 3. 索引失效

下面这些写法，明明列上有索引，却用不上。

### 3.1 在列上使用函数或表达式

```
type   key          key_len  rows    Extra
range  idx_created  5          1000  Using index condition  ← WHERE created_at >= '2026-09-01' AND created_at < '2026-09-02'
ALL    NULL         NULL     996300  Using where            ← WHERE DATE(created_at) = '2026-09-01'
ALL    NULL         NULL     996300  Using where            ← WHERE id + 1 = 500001
```

```
毫秒：1 0.61 0.57         ← WHERE created_at >= '2026-09-01' AND created_at < '2026-09-02'
毫秒：86.73 93.72 86.76   ← WHERE DATE(created_at) = '2026-09-01'
```

两条查询的结果相同（都是 1000 行），一个用了索引，一个扫描了全表。索引中存的是 `created_at` 的值，按它排序；`DATE(created_at)` 的值不在索引中，数据库不能假设函数的结果和原来的值有同样的顺序，只能对每一行计算一遍。`id + 1 = 500001` 也一样，要写成 `id = 500000`。

**改写的原则：让索引列「单独」出现在比较的一边**，把计算移到常量那边。

如果实在要按函数的结果查询，MySQL 8.0.13 起可以建**函数索引**，对表达式的结果建索引：

```sql
CREATE INDEX idx_created_date ON u ((DATE(created_at)));    -- 注意两层括号
```

```
type   key               key_len  rows  Extra
ref    idx_created_date  4        1000  NULL      ← WHERE DATE(created_at) = '2026-09-01'
```

```
毫秒：1.13 0.78 0.54
```

### 3.2 隐式类型转换

`phone` 是字符串，查询时却传了一个数字：

```
type   key        key_len  rows    Extra
ref    idx_phone  46            1  NULL         ← WHERE phone = '13800500000'
ALL    NULL       NULL     996300  Using where  ← WHERE phone = 13800500000
const  PRIMARY    4             1  NULL         ← WHERE id = '500000'
```

```
毫秒：0.05 0.03 0.03      ← WHERE phone = '13800500000'
毫秒：88.74 85.03 84.84   ← WHERE phone = 13800500000
```

差一对引号，慢了近 3000 倍，而且结果一样，不会有任何报错，很难发现。

字符串和数字比较时，MySQL 把**两边都转换成数字**再比较。对 `phone` 列来说，相当于在每一行上执行了 `CAST(phone AS DOUBLE)`，和 3.1 节在列上用函数一样，索引就失效了。而且这种转换不是一一对应的：`'13800500000'`、`'013800500000'`、`'13800500000 '` 转换成数字都相等，所以数据库也不能把数字转换成某一个字符串再查索引。

反过来，`id` 是整数，传字符串 `'500000'` 却没有问题：需要转换的是常量 `'500000'`，转换一次就行，`id` 列不受影响。

这种问题最常见于程序中：一个变量在一种语言中是数字，拼进 SQL 后没有引号（或者用参数绑定时类型是整数）。手机号、身份证号、订单号这类「号码」，在表中是字符串，查询时也要用字符串。

### 3.3 LIKE 以 % 开头

```
type   key        key_len  rows    Extra
range  idx_phone  46           10  Using index condition  ← WHERE phone LIKE '1380050000%'
ALL    NULL       NULL     996300  Using where            ← WHERE phone LIKE '%0500000'
```

```
毫秒：0.05 0.03 0.03      ← WHERE phone LIKE '1380050000%'
毫秒：109.04 108.72 108.39 ← WHERE phone LIKE '%0500000'
```

前缀确定的 `LIKE 'abc%'` 相当于范围条件 `>= 'abc' AND < 'abd'`，可以用索引；以 `%` 开头时，不知道从哪里开始找，只能逐行匹配。要按「包含某个词」搜索大量文本，应该用全文索引（MySQL 的 `FULLTEXT`）或者专门的搜索引擎。

### 3.4 其他

- **`<>`、`NOT IN`**：「不等于某个值」通常意味着几乎所有的行，优化器一般选择全表扫描（实测 `phone <> '13800500000'` 是 `ALL`）；
- **`OR` 中有一个分支没有索引**：只能全表扫描（上一篇练习 1）；
- **连接的两个列类型或字符集不同**：例如一个是 `INT`、一个是 `VARCHAR`，或者一个是 utf8mb4、一个是 latin1，相当于在列上做了转换；
- **统计信息过时**：优化器根据估计选择执行方式，大量修改数据后，用 `ANALYZE TABLE` 更新统计信息。

## 4. 区分度低的列

`gender` 只有两种取值，各占一半。在它上面建了索引：

```
type  key         rows    Extra
ref   idx_gender  498150  Using index condition  ← SELECT SUM(age) FROM u WHERE gender = 'F'
```

```
毫秒：176.62 159.03 159.01  ← SELECT SUM(age) FROM u WHERE gender = 'F'
毫秒：56.57 57.24 56.84     ← SELECT SUM(age) FROM u IGNORE INDEX (idx_gender) WHERE gender = 'F'
```

优化器选择了这个索引，结果比全表扫描慢了近 3 倍：要回表 50 万次，每次随机地在聚簇索引中走 3 层，而全表扫描只是顺序读一遍。上一篇的 `k > 100000` 中优化器选对了，这一次它选错了，它的代价估计并不总是准确的。

一个列有多少种不同的值，叫作它的**区分度**（cardinality，`SHOW INDEX FROM u` 中的 `Cardinality` 列是它的估计值）。区分度很低的列（性别、状态、是否删除），单独建索引通常没有用，甚至有害：占空间、拖慢写入，还可能误导优化器。它们适合作为联合索引中的一列，和其他条件一起使用，比如 `(gender, city, age)`，或者放在一个覆盖索引中。

## 5. 怎样设计索引

1. **从查询出发**：先列出最重要的、执行最频繁的查询（上一篇的 `performance_schema` 统计），为它们设计索引，而不是为每一列建一个索引；
2. **等值条件的列在前，范围条件的列在后，排序的列紧跟在等值条件之后**：`WHERE city = ? AND age = ? ORDER BY name` 用 `(city, age, name)`；
3. **能覆盖就覆盖**：把查询需要的少数几列也放进索引，避免回表。但不要把所有的列都放进去，索引会和表一样大；
4. **一个联合索引可以代替多个单列索引**：有了 `(city, age, name)`，就不需要单独的 `(city)` 和 `(city, age)`，它们是它的前缀；
5. **区分度低的列不要单独建索引**；
6. **删除用不上的索引**：`sys.schema_unused_indexes` 列出了自上次启动以来没有被使用过的索引；
7. **改写查询，而不是迁就错误的写法**：不在列上用函数，类型要一致，避免以 `%` 开头的 `LIKE`。

## 6. 常见错误

- **跳过联合索引的第一列**：`(city, age, name)` 上查 `age = 30`；
- **范围条件放在联合索引的中间**：后面的列只能过滤，不能定位；
- **在索引列上用函数或计算**：`DATE(created_at) = …`、`id + 1 = …`；
- **字符串列和数字比较**：差一对引号，全表扫描；
- **`LIKE '%…'`**；
- **在区分度低的列上建单列索引**，甚至以为「建了索引就一定快」；
- **给每一列都建一个单列索引**：一条查询通常只用一个索引（索引合并是例外），多个单列索引不能代替一个联合索引。

## 7. 小结

- 联合索引按第一列、第二列……的顺序排序；只能从最左边的列开始连续使用（最左前缀），`key_len` 显示用到了前几列
- 范围条件之后的列不能再用来定位；把范围改成 `IN` 列表，实测从约 0.9 毫秒降到约 0.06 毫秒
- 索引条件下推（`Using index condition`）在索引中先过滤，减少回表；跳跃扫描（`Using index for skip scan`）在第一列取值很少时，可以跳过它
- `WHERE` 中的等值列加上 `ORDER BY` 的列组成索引的前缀，就不需要排序
- 索引失效：列上用函数（函数索引可以解决）、隐式类型转换、`LIKE '%…'`，都从 0.03 毫秒变成了 90 毫秒左右
- 区分度低的列上的索引会误导优化器，实测比全表扫描慢近 3 倍

## 8. 练习

1. 有了 `idx_city_age_name`，下面哪些查询能用它来定位（不只是跳跃扫描）？分别用到了哪几列？
   - `WHERE city = '北京' AND age BETWEEN 20 AND 30`
   - `WHERE name = 'name0000853'`
   - `WHERE city IN ('北京', '上海') AND age = 30`
   - `WHERE city LIKE '北%' AND age = 30`
2. `WHERE city = '北京' ORDER BY age DESC, name DESC LIMIT 10` 和 `ORDER BY age, name DESC` 能用上 `idx_city_age_name` 来避免排序吗？
3. 一个查询是 `SELECT id, name FROM u WHERE city = ? AND gender = ? ORDER BY created_at DESC LIMIT 20`，设计一个索引。

<details>
<summary>点击查看答案</summary>

1. - `city = '北京' AND age BETWEEN 20 AND 30`：能，用到 `city` 和 `age` 两列（`key_len` 43），`age` 是范围条件；
   - `name = 'name0000853'`：不能，缺少第一列 `city`（也缺少 `age`），只能全表扫描；
   - `city IN ('北京', '上海') AND age = 30`：能，`IN` 相当于两个等值条件，两列都用上了；
   - `city LIKE '北%' AND age = 30`：`LIKE '北%'` 是范围条件，能用 `city` 定位，但 `age` 不能缩小范围（和 2.2 节的 `age > 70` 之后的 `name` 一样）。实测的 `EXPLAIN` 有点迷惑性：`key_len` 是 43，看起来用上了 `age`，但估计的行数是 98082，和只有 `city = '北京'` 时完全相同。`age` 只是出现在了范围的边界中，「北京、30 岁」和「北京以后、30 岁」之间仍然包括了北京的所有年龄，真正起作用的是索引条件下推对 `age` 的过滤。对比 `city IN ('北京', '上海') AND age = 30`，估计只有 1588 行，那才是两列都用来定位了。
2. 实测：

   ```
   type  key                rows   Extra
   ref   idx_city_age_name  98082  Backward index scan  ← ORDER BY age DESC, name DESC LIMIT 10
   ref   idx_city_age_name  98082  Using filesort       ← ORDER BY age, name DESC LIMIT 10
   ```

   ```
   毫秒：1.42 0.79 0.73      ← ORDER BY age DESC, name DESC LIMIT 10
   毫秒：48.17 33.35 31.92   ← ORDER BY age, name DESC LIMIT 10
   ```

   两列都降序时，从后往前读索引就是需要的顺序（`Backward index scan`）。一升一降时，索引中没有这样的顺序，要排序。MySQL 8.0 起支持**降序索引**，建一个 `(city, age, name DESC)` 的索引，第二条查询也不需要排序了，实测降到 0.4 毫秒。
3. `city` 和 `gender` 都是等值条件，放在前面；`created_at` 用于排序，紧跟其后；查询还要 `name`，加进去可以避免回表（`id` 是主键，二级索引中本来就有）：

   ```sql
   CREATE INDEX idx_city_gender_created ON u (city, gender, created_at, name);
   ```

   实测：

   ```
   type  key                      key_len  Extra
   ref   idx_city_gender_created  46       Using where; Backward index scan; Using index
   ```

   ```
   毫秒：0.06 0.03 0.02      ← 使用这个索引
   毫秒：676.71 550.6 530.19 ← IGNORE INDEX (idx_city_gender_created)
   ```

   查询时定位到「某城市、某性别」那一段，这一段按 `created_at` 排序，从后往前读 20 条就完成了（`Backward index scan`），既不需要排序，也不需要回表（`Using index`）。`gender` 单独区分度很低，但放在联合索引中，和 `city` 一起把范围缩小到了 1/40。`city` 和 `gender` 谁在前面都可以，因为它们都是等值条件；如果还有只按 `city` 查询的场景，把 `city` 放在前面，这个索引就能同时为它服务。

</details>
