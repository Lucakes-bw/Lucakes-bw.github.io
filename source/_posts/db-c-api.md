---
title: 用 C 访问 MySQL
date: 2026-10-25 11:20:00
updated: 2026-10-25 11:20:00
categories:
  - 数据库
  - 应用与运维
tags:
  - 数据库
  - MySQL
  - C
  - 安全
order: 1
description: 程序怎样使用数据库？本文用 MySQL 的 C 客户端库写几个小程序：连接、查询、逐行读取结果。然后写一个登录检查，用拼接字符串的方式构造 SQL，实测四种 SQL 注入：输入 admin' -- 不需要密码就以管理员身份登录，用 UNION 读出了管理员的密码哈希；换成预处理语句后，这些输入全部失效。最后在 C 中实现一个带回滚的转账，并用一个只有增删改查权限的账号连接数据库。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会用 libmysqlclient 连接数据库、执行查询、读取结果、处理错误；理解 SQL 注入的原理，能用实验说明它的危害；会使用预处理语句；会在程序中正确地使用事务；理解最小权限原则。
>
> **前置知识**：C 语言专栏（指针、字符串、结构体）；本专栏第 1 章（用户和权限）和第 4 章（事务）；计算机网络专栏的 TCP 连接。
>
> **实验环境**：服务器是前几章的 MySQL 8.4.13 容器（IP 地址 172.17.0.4）；程序在另一个 Ubuntu 24.04 容器中编译运行，GCC 13.3，客户端库 libmysqlclient 8.0.46（`apt install libmysqlclient-dev`）。程序和数据库通过网络连接，就像真实的应用服务器和数据库服务器一样。

## 1. 准备

建一个应用的数据库，有一个用户表。密码不存明文，存的是它的哈希值（这里为了简单用 SHA-256，真实的系统应该用 bcrypt 这类专门为密码设计的、计算很慢的算法，第 2 章《实战：设计一个博客数据库》）：

```sql
CREATE DATABASE app;
USE app;
CREATE TABLE users (
    id            INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    username      VARCHAR(30) NOT NULL UNIQUE,
    password_hash CHAR(64) NOT NULL,
    nickname      VARCHAR(30) NOT NULL,
    balance       DECIMAL(10, 2) NOT NULL DEFAULT 0 CHECK (balance >= 0)
);
INSERT INTO users (username, password_hash, nickname, balance) VALUES
    ('admin', SHA2('Adm1n!2026', 256), '管理员', 0),
    ('alice', SHA2('alice123', 256), '爱丽丝', 100),
    ('bob',   SHA2('bob456', 256),   '鲍勃', 50);
```

应用程序**不用 root 连接**，而是用一个只有增删改查权限的账号（第 1 章第 6 节）：

```sql
CREATE USER 'app'@'%' IDENTIFIED BY 'app-pass';
GRANT SELECT, INSERT, UPDATE, DELETE ON app.* TO 'app'@'%';
```

## 2. 连接和查询

把连接的代码放在一个头文件中，后面的程序都用它：

```c db.h
// db.h：连接数据库的公共函数
#ifndef DB_H
#define DB_H
#include <mysql.h>
#include <stdio.h>
#include <stdlib.h>

// 连接 app 库；失败时打印原因并退出
static inline MYSQL *db_connect(void)
{
    MYSQL *conn = mysql_init(NULL);
    if (conn == NULL) {
        fprintf(stderr, "mysql_init 失败\n");
        exit(1);
    }
    mysql_options(conn, MYSQL_SET_CHARSET_NAME, "utf8mb4");     // 连接字符集（1.2 篇）
    if (mysql_real_connect(conn, "172.17.0.4", "app", "app-pass", "app", 3306, NULL, 0) == NULL) {
        fprintf(stderr, "连接失败：%s\n", mysql_error(conn));
        mysql_close(conn);
        exit(1);
    }
    return conn;
}
#endif
```

- `mysql_init` 创建一个连接对象；
- `mysql_options` 在连接之前设置选项。**一定要设置连接字符集**，否则可能出现第 1 章那样的双重编码；
- `mysql_real_connect` 建立 TCP 连接（第 1 章看到的握手包就是在这时收到的），完成认证，选择数据库。失败时返回 `NULL`，`mysql_error` 给出原因。

（真实的程序不会把密码写在代码中，而是从配置文件或环境变量中读取。）

查询所有用户：

```c list.c
// list.c：查询所有用户并逐行打印
#include "db.h"

int main(void)
{
    MYSQL *conn = db_connect();
    printf("服务器版本 %s，客户端库版本 %s\n", mysql_get_server_info(conn), mysql_get_client_info());

    if (mysql_query(conn, "SELECT id, username, nickname, balance FROM users ORDER BY id") != 0) {
        fprintf(stderr, "查询失败：%s\n", mysql_error(conn));
        return 1;
    }
    MYSQL_RES *res = mysql_store_result(conn);          // 把整个结果取到客户端
    printf("共 %llu 行，%u 列\n", (unsigned long long)mysql_num_rows(res), mysql_num_fields(res));
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != NULL) {      // row 是一个字符串数组，NULL 值是空指针
        printf("%s  %-8s %s  %s\n", row[0], row[1], row[2], row[3]);
    }
    mysql_free_result(res);
    mysql_close(conn);
    return 0;
}
```

编译时用 `mysql_config` 得到头文件和库的路径：

```bash
gcc -Wall -Wextra -O2 $(mysql_config --cflags) list.c -o list $(mysql_config --libs)
./list
```

```
服务器版本 8.4.13，客户端库版本 8.0.46
共 3 行，4 列
1  admin    管理员  0.00
2  alice    爱丽丝  100.00
3  bob      鲍勃  50.00
```

几点说明：

- `mysql_query` 把 SQL 发给服务器执行，返回 0 表示成功；
- `mysql_store_result` 把**全部**结果读到客户端的内存中。结果很大时，可以用 `mysql_use_result` 逐行从服务器读取，但在读完之前，这个连接不能执行别的语句；
- 每一行是一个字符串数组：**不论列的类型是什么，值都以字符串的形式返回**，`balance` 是 `"100.00"`，需要数值时自己转换；值为 `NULL` 时，对应的指针是 `NULL`，使用前要检查（这里的列都是 `NOT NULL`）；
- 结果用完要 `mysql_free_result`，连接用完要 `mysql_close`，否则会泄漏内存，服务器端的连接也会一直占着。

## 3. SQL 注入

### 3.1 拼接字符串

写一个登录检查：用户输入用户名和密码，在数据库中查找匹配的用户。最直接的写法是把输入拼进 SQL：

```c login_bad.c
// login_bad.c：用拼接字符串的方式检查用户名和密码（有 SQL 注入漏洞，不要这样写）
#include "db.h"

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "用法：%s 用户名 密码\n", argv[0]);
        return 2;
    }
    MYSQL *conn = db_connect();
    char sql[512];
    snprintf(sql, sizeof sql,
             "SELECT id, nickname FROM users WHERE username = '%s' AND password_hash = SHA2('%s', 256)",
             argv[1], argv[2]);
    printf("执行的 SQL：%s\n", sql);
    if (mysql_query(conn, sql) != 0) {
        printf("查询出错：%s\n", mysql_error(conn));
        return 1;
    }
    MYSQL_RES *res = mysql_store_result(conn);
    MYSQL_ROW row = mysql_fetch_row(res);
    if (row != NULL) {
        printf("登录成功：id = %s，昵称 = %s\n", row[0], row[1]);
    } else {
        printf("用户名或密码错误\n");
    }
    mysql_free_result(res);
    mysql_close(conn);
    return 0;
}
```

正常使用时没有问题：

```
--- 正确的密码
执行的 SQL：SELECT id, nickname FROM users WHERE username = 'alice' AND password_hash = SHA2('alice123', 256)
登录成功：id = 2，昵称 = 爱丽丝
--- 错误的密码
执行的 SQL：SELECT id, nickname FROM users WHERE username = 'alice' AND password_hash = SHA2('wrong', 256)
用户名或密码错误
```

但用户名是用户输入的，它可以包含任何字符，包括单引号。

### 3.2 四种攻击

**注释掉密码检查。** 用户名输入 `admin' -- `，密码随便填：

```
执行的 SQL：SELECT id, nickname FROM users WHERE username = 'admin' -- ' AND password_hash = SHA2('anything', 256)
登录成功：id = 1，昵称 = 管理员
```

输入中的单引号结束了字符串，`-- `（两个减号加一个空格）是 SQL 的注释，把后面的密码检查整个注释掉了。不知道密码，就以管理员的身份登录了。

**让条件永远为真。** 用户名输入 `x' OR 1=1 -- `：

```
执行的 SQL：SELECT id, nickname FROM users WHERE username = 'x' OR 1=1 -- ' AND password_hash = SHA2('anything', 256)
登录成功：id = 1，昵称 = 管理员
```

`OR 1=1` 让每一行都满足条件，程序取第一行，恰好又是管理员。连用户名都不需要知道。

**读出别的数据。** 用户名输入 `x' UNION SELECT 1, CONCAT(username, ':', password_hash) FROM users WHERE username = 'admin' -- `：

```
执行的 SQL：SELECT id, nickname FROM users WHERE username = 'x' UNION SELECT 1, CONCAT(username, ':', password_hash) FROM users WHERE username = 'admin' -- ' AND password_hash = SHA2('x', 256)
登录成功：id = 1，昵称 = admin:ba4075c612a6718afa7433b9b2bac6c32eeeb333fb5c1cb87d8896ae192c8b2b
```

`UNION` 拼上了攻击者自己的查询（第 1 章《多表查询》），程序把它的结果当作「昵称」显示了出来：管理员的密码哈希泄露了。用同样的方法，可以读出这个账号有权限读的任何表。

**执行第二条语句。** 用户名输入 `x'; DROP TABLE users; -- `：

```
执行的 SQL：SELECT id, nickname FROM users WHERE username = 'x'; DROP TABLE users; -- ' AND password_hash = SHA2('x', 256)
查询出错：You have an error in your SQL syntax; check the manual that corresponds to your MySQL server version for the right syntax to use near 'DROP TABLE users; -- ' AND password_hash = SHA2('x', 256)' at line 1
```

这次没有成功：`mysql_query` 默认一次只能执行一条语句，分号之后的部分被当作语法错误。（连接时指定了 `CLIENT_MULTI_STATEMENTS` 选项才允许多条语句。）即使允许了，`app` 账号也没有 `DROP` 权限：

```
ERROR 1142 (42000): DROP command denied to user 'app'@'127.0.0.1' for table 'users'
```

这就是最小权限的意义：程序有漏洞时，攻击者能做的事情也被限制在这个账号的权限之内。但它只是**最后一道防线**，前三种攻击都只用到了 `SELECT` 权限。

**SQL 注入的本质是：程序把用户的输入当作了 SQL 代码的一部分。** 输入中的引号、注释、关键字，都改变了 SQL 的结构。

### 3.3 预处理语句

解决办法是**不让输入成为 SQL 的一部分**。预处理语句把 SQL 和数据分开发送：先发送带 `?` 占位符的 SQL，服务器解析它、确定它的结构；再单独发送每个 `?` 的值，这些值只会被当作数据，不会再被解析：

```c login_good.c
// login_good.c：用预处理语句检查用户名和密码
#include <string.h>
#include "db.h"

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "用法：%s 用户名 密码\n", argv[0]);
        return 2;
    }
    MYSQL *conn = db_connect();
    // 1. 准备：SQL 中用 ? 占位，先发给服务器解析
    const char *sql = "SELECT id, nickname FROM users WHERE username = ? AND password_hash = SHA2(?, 256)";
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        fprintf(stderr, "准备失败：%s\n", mysql_stmt_error(stmt));
        return 1;
    }
    // 2. 绑定参数：告诉客户端库每个 ? 的类型和数据在哪里
    unsigned long len[2] = {strlen(argv[1]), strlen(argv[2])};
    MYSQL_BIND param[2];
    memset(param, 0, sizeof param);
    for (int i = 0; i < 2; i++) {
        param[i].buffer_type = MYSQL_TYPE_STRING;
        param[i].buffer = argv[i + 1];
        param[i].buffer_length = len[i];
        param[i].length = &len[i];
    }
    mysql_stmt_bind_param(stmt, param);
    // 3. 绑定结果：每一列放进哪个变量
    int id;
    char nickname[64];
    unsigned long nick_len;
    MYSQL_BIND result[2];
    memset(result, 0, sizeof result);
    result[0].buffer_type = MYSQL_TYPE_LONG;
    result[0].buffer = &id;
    result[1].buffer_type = MYSQL_TYPE_STRING;
    result[1].buffer = nickname;
    result[1].buffer_length = sizeof nickname;
    result[1].length = &nick_len;
    mysql_stmt_bind_result(stmt, result);
    // 4. 执行，取结果
    if (mysql_stmt_execute(stmt) != 0) {
        fprintf(stderr, "执行失败：%s\n", mysql_stmt_error(stmt));
        return 1;
    }
    if (mysql_stmt_fetch(stmt) == 0) {
        printf("登录成功：id = %d，昵称 = %.*s\n", id, (int)nick_len, nickname);
    } else {
        printf("用户名或密码错误\n");
    }
    mysql_stmt_close(stmt);
    mysql_close(conn);
    return 0;
}
```

同样的输入：

```
--- 正确的密码
登录成功：id = 2，昵称 = 爱丽丝
--- 错误的密码
用户名或密码错误
--- 注释掉密码检查
用户名或密码错误
--- OR 1=1
用户名或密码错误
--- UNION 读出哈希
用户名或密码错误
--- 第二条语句
用户名或密码错误
```

全部失效。`admin' -- ` 被当作一个完整的用户名去查找，当然找不到这个用户。

C 的预处理接口比较繁琐：每个参数和每个结果列都要填一个 `MYSQL_BIND` 结构，说明类型、缓冲区和长度（第 2、3 步）。注意结果是直接写进程序提供的变量的，`id` 是 `int`，不再是字符串；字符串的长度通过 `length` 返回，缓冲区不保证以 `\0` 结尾，所以打印时用 `%.*s`。其他语言的数据库库把这些都包装好了，例如 Python 的 `cursor.execute("… WHERE username = %s", (name,))`，原理相同。

还有一个函数 `mysql_real_escape_string`，它在单引号等特殊字符前加上反斜杠，也能防止注入。但它要求每一处拼接都记得调用，漏掉一处就有漏洞，而且对拼在引号之外的输入（例如 `LIMIT %s`、`ORDER BY %s`）无效。**预处理语句是首选。**

注意：`?` 只能代替**值**，不能代替表名、列名、`ORDER BY` 的方向这些 SQL 结构。如果这些部分要根据用户的选择变化，程序应该从一个固定的列表中选择（例如用户选择「按价格排序」，程序就使用写死在代码中的 `ORDER BY price`），而不是把用户的输入拼进去。

## 4. 在程序中使用事务

第 4 章强调过：语句出错时事务不会自动回滚，程序必须自己判断、自己回滚。C 中的转账：

```c transfer.c
// transfer.c：在事务中转账，任何一步失败都回滚
#include <string.h>
#include "db.h"

// 执行一条带两个参数（金额、用户名）的 UPDATE，返回影响的行数，出错返回 -1
static long long exec_update(MYSQL *conn, const char *sql, const char *amount, const char *user)
{
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0) {
        fprintf(stderr, "准备失败：%s\n", mysql_stmt_error(stmt));
        mysql_stmt_close(stmt);
        return -1;
    }
    unsigned long len[2] = {strlen(amount), strlen(user)};
    MYSQL_BIND param[2];
    memset(param, 0, sizeof param);
    param[0].buffer_type = MYSQL_TYPE_STRING;           // 金额以字符串传给 DECIMAL，避免浮点误差
    param[0].buffer = (char *)amount;
    param[0].length = &len[0];
    param[1].buffer_type = MYSQL_TYPE_STRING;
    param[1].buffer = (char *)user;
    param[1].length = &len[1];
    mysql_stmt_bind_param(stmt, param);
    long long rows = -1;
    if (mysql_stmt_execute(stmt) == 0) {
        rows = (long long)mysql_stmt_affected_rows(stmt);
    } else {
        fprintf(stderr, "  执行失败：%s\n", mysql_stmt_error(stmt));
    }
    mysql_stmt_close(stmt);
    return rows;
}

// 从 from 转 amount 给 to，成功返回 0
static int transfer(MYSQL *conn, const char *from, const char *to, const char *amount)
{
    mysql_autocommit(conn, 0);                          // 开始事务（关闭自动提交）
    long long r1 = exec_update(conn, "UPDATE users SET balance = balance - ? WHERE username = ?", amount, from);
    long long r2 = r1 == 1 ? exec_update(conn, "UPDATE users SET balance = balance + ? WHERE username = ?", amount, to) : -1;
    int ok = r1 == 1 && r2 == 1;                        // 两条都恰好修改了 1 行才算成功
    if (ok) {
        ok = mysql_commit(conn) == 0;
    }
    if (!ok) {
        mysql_rollback(conn);
    }
    mysql_autocommit(conn, 1);
    return ok ? 0 : -1;
}

static void show(MYSQL *conn)
{
    mysql_query(conn, "SELECT username, balance FROM users WHERE username IN ('alice', 'bob') ORDER BY id");
    MYSQL_RES *res = mysql_store_result(conn);
    MYSQL_ROW row;
    printf("  余额：");
    while ((row = mysql_fetch_row(res)) != NULL) {
        printf("%s %s  ", row[0], row[1]);
    }
    printf("\n");
    mysql_free_result(res);
}

int main(int argc, char *argv[])
{
    if (argc != 4) {
        fprintf(stderr, "用法：%s 转出 转入 金额\n", argv[0]);
        return 2;
    }
    MYSQL *conn = db_connect();
    show(conn);
    printf("%s 转给 %s %s 元：%s\n", argv[1], argv[2], argv[3],
           transfer(conn, argv[1], argv[2], argv[3]) == 0 ? "成功" : "失败，已回滚");
    show(conn);
    mysql_close(conn);
    return 0;
}
```

三次运行：

```
$ ./transfer alice bob 30
  余额：alice 100.00  bob 50.00
alice 转给 bob 30 元：成功
  余额：alice 70.00  bob 80.00
$ ./transfer alice bob 500
  执行失败：Check constraint 'users_chk_1' is violated.
alice 转给 bob 500 元：失败，已回滚
  余额：alice 70.00  bob 80.00
$ ./transfer alice nobody 10
alice 转给 nobody 10 元：失败，已回滚
  余额：alice 70.00  bob 80.00
```

- 余额不足：扣款的 `UPDATE` 违反了 `CHECK` 约束，出错，回滚；
- 转给不存在的用户：两条 `UPDATE` 都**没有出错**，但加款的那条影响了 0 行。如果只检查「有没有出错」，扣款就被提交了，alice 白白少了 10 元。所以程序检查的是「是否恰好修改了 1 行」（`mysql_stmt_affected_rows`），不满足就回滚。

这是程序中使用事务的要点：**成功的标准由业务决定**，数据库只报告语句是否出错、影响了几行，判断这是否符合预期、该提交还是回滚，是程序的责任。

另外，扣款用的是 `balance = balance - ?`，而不是先读出余额、在程序中计算、再写回去，这样不会丢失更新（4.2 篇）。

## 5. 常见错误

- **拼接用户的输入构造 SQL**：任何来自外部的数据（表单、网址参数、HTTP 头、文件）都可能被用来注入，一律用预处理语句；
- **以为过滤掉单引号就安全了**：数字类型的参数根本不需要引号（`WHERE id = 1 OR 1=1`），各种编码和转义的绕过方法层出不穷；
- **应用程序用 root 连接**：一旦有注入漏洞，整个数据库都暴露了；
- **不检查返回值**：`mysql_query` 失败时 `mysql_store_result` 返回 `NULL`，直接使用会崩溃；
- **不设置连接字符集**：中文被双重编码；
- **把值当作 C 字符串直接使用**：`NULL` 值是空指针；预处理语句返回的字符串不以 `\0` 结尾；
- **只检查语句是否出错，不检查影响的行数**：0 行也是「成功」；
- **不释放结果和连接**：内存泄漏，服务器端的连接被占满（`max_connections`）。

## 6. 小结

- libmysqlclient：`mysql_init`、`mysql_options`（设置 utf8mb4）、`mysql_real_connect`、`mysql_query`、`mysql_store_result`、`mysql_fetch_row`；值都以字符串返回，`NULL` 是空指针
- SQL 注入：程序把输入当成了 SQL 代码。实测 `admin' -- ` 绕过密码以管理员身份登录，`UNION` 读出了密码哈希
- 预处理语句把 SQL 和数据分开发送，所有的注入都失效了；`?` 只能代替值，不能代替表名、列名
- 最小权限：应用账号只给需要的权限，`DROP` 被拒绝，但它只是最后一道防线
- 程序中的事务：关闭自动提交，按业务判断成功与否（包括影响的行数），失败就回滚

## 7. 练习

1. `login_bad` 中，如果把用户名的输入改为只允许字母和数字，还能注入吗？密码呢？
2. 一个商品列表页按用户选择的列排序，程序写成 `snprintf(sql, …, "SELECT … ORDER BY %s", sort)`。能把 `sort` 换成预处理语句的 `?` 吗？应该怎样写？
3. `transfer` 中，如果 `mysql_commit` 返回了错误（比如网络在这时断开），程序回滚了。这笔转账一定没有发生吗？

<details>
<summary>点击查看答案</summary>

1. 用户名不能再注入了，但密码同样被拼进了 SQL。用户名填 `alice`，密码填 `x', 256) OR 1=1 -- `，实测：

   ```
   执行的 SQL：SELECT id, nickname FROM users WHERE username = 'alice' AND password_hash = SHA2('x', 256) OR 1=1 -- ', 256)
   登录成功：id = 1，昵称 = 管理员
   ```

   密码中的 `', 256)` 补全了 `SHA2` 的调用，`OR 1=1` 让条件永远为真（`AND` 比 `OR` 优先，第 1 章），连以谁的身份登录都变成了管理员。`login_good` 对同样的输入返回「用户名或密码错误」。每一个被拼接的输入都要处理，漏掉一个就有漏洞，这正是「过滤输入」难以做对的原因。预处理语句对所有的参数一视同仁，不存在漏掉的问题。（输入校验仍然有价值，比如限制用户名的格式，但它是业务规则，不是防注入的手段。）
2. 不能。`?` 只能代表一个值，`ORDER BY ?` 等于按一个常量排序，不会按那一列排序。应该把用户的选择映射到固定的 SQL 片段：

   ```c
   const char *order;
   if (strcmp(sort, "price") == 0) order = "price";
   else if (strcmp(sort, "name") == 0) order = "name";
   else order = "id";                       // 其他任何输入都用默认的排序
   snprintf(sql, sizeof sql, "SELECT id, name, price FROM product ORDER BY %s LIMIT 20", order);
   ```

   拼进 SQL 的只可能是代码中写死的三个字符串之一，用户的输入只是用来**选择**，不会进入 SQL。
3. 不一定。和 4.4 篇第 5 节一样，`COMMIT` 已经发到了服务器，服务器可能已经提交了，只是回复没有传回来；这时 `mysql_rollback` 也发不出去（连接已经断了）。程序应该重新连接后查询确认，例如给每笔转账一个唯一的编号，记录在一个转账记录表中（和两个 `UPDATE` 在同一个事务里），重连后按编号查询这笔转账是否存在。

</details>
