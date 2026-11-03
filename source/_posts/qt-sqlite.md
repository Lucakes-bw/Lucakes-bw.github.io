---
title: SQLite 本地存储
date: 2026-10-29 12:00:00
updated: 2026-10-29 12:00:00
categories:
  - 客户端开发
  - 网络与数据
tags:
  - Qt
  - C++
  - SQLite
  - 数据库
order: 4
description: 聊天记录要保存在本地，用 Qt 自带的 SQLite 驱动，不需要安装任何服务器。逐条插入 1000 条消息，每条自动提交，用了 140～190 毫秒；放在一个事务中只要 1 毫秒，快了 150 多倍。消息是一条一条到达的，没法攒成大事务，打开 WAL 日志后，逐条提交从每条 0.14 毫秒降到 0.03 毫秒，再把同步级别降为 NORMAL，降到 0.007 毫秒。名字中有一个单引号的用户 O'Brien，拼接出来的 SQL 直接报语法错误；构造过的输入 x' OR '1'='1 让 count 变成了整张表的 100 万；绑定参数后都正常。在 100 万条消息中查一个会话最近的 50 条，没有索引要 22 毫秒，建了联合索引后只要 0.01 毫秒。还有两个坑：执行 PRAGMA 的 QSqlQuery 没有结束时，后面的 1000 条插入都没有提交，另一个连接报 database is locked；在工作线程中使用主线程的连接，Qt 拒绝执行。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会用 Qt SQL 模块打开 SQLite 数据库、建表、增删改查；会用预处理语句和绑定参数，避免 SQL 注入；理解事务对写入速度的影响，知道 WAL 模式和同步级别；会为常用的查询建索引并用 `EXPLAIN QUERY PLAN` 验证；知道一个数据库连接只能在创建它的线程中使用；知道数据库文件应该放在哪里。
>
> **前置知识**：数据库专栏第 1 章（SQL 基础）、《B+ 树索引》《联合索引与索引失效》《事务与 ACID》《日志与崩溃恢复》《用 C 访问 MySQL》（SQL 注入与预处理语句）；本专栏第三章（多线程）。
>
> **实验环境**：macOS 27.0（Apple M6，SSD），Qt 6.12.0 自带的 SQLite 3.54.0，Release 构建。用到 Qt SQL 模块：`find_package(Qt6 REQUIRED COMPONENTS Sql)`，链接 `Qt6::Sql`。

## 1. 为什么用 SQLite

客户端也要保存数据：聊天记录、联系人、草稿、设置。每次打开程序都从服务器重新下载所有聊天记录显然不行：慢、费流量，没有网络时什么也看不到。

数据库专栏用的 MySQL 是一个**服务器**：单独运行一个进程，客户端通过网络连接它。在用户的电脑上装一个 MySQL 服务器来存聊天记录，显然不现实。**SQLite** 是一个**嵌入式**数据库：它就是一个库，链接进程序里，整个数据库是一个普通的文件，不需要安装、配置、启动任何服务。微信、浏览器（书签和历史记录）、手机上的大部分应用都用它保存本地数据。

SQLite 支持标准的 SQL、事务、索引，数据库专栏学过的知识大部分都能直接用上。Qt 自带了 SQLite 的驱动，不需要另外安装：

```cpp
std::printf("可用的驱动：%s\n", qPrintable(QSqlDatabase::drivers().join(" ")));
```

```
可用的驱动：QSQLITE
```

Homebrew 安装的 Qt 只带了 SQLite 驱动；Qt 还提供 MySQL（`QMYSQL`）、PostgreSQL（`QPSQL`）等驱动，需要另外编译。

## 2. 基本用法

### 2.1 打开数据库、建表

```cpp
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
db.setDatabaseName("chat.db");            // 文件不存在时会自动创建
if (!db.open()) {
    std::printf("打开失败：%s\n", qPrintable(db.lastError().text()));
    return 1;
}

QSqlQuery q;
q.exec("CREATE TABLE message ("
       " id INTEGER PRIMARY KEY,"
       " peer TEXT NOT NULL,"           // 和谁的会话
       " sender TEXT NOT NULL,"
       " body TEXT NOT NULL,"
       " sent_at INTEGER NOT NULL)");   // 发送时间（毫秒）
```

- `addDatabase("QSQLITE")` 创建一个**连接**。不指定连接名时，它是「默认连接」，之后创建的 `QSqlQuery` 不传数据库参数，就使用默认连接。
- SQLite 的 `INTEGER PRIMARY KEY` 就是自增的主键（相当于 MySQL 的 `AUTO_INCREMENT`）。
- 时间用整数保存（自 1970 年起的毫秒数），排序和比较都很快，也没有时区的问题。

### 2.2 查询

```cpp
QSqlQuery q;
q.prepare("SELECT sender, body FROM message WHERE peer = ? ORDER BY sent_at DESC LIMIT 50");
q.addBindValue("user42");
q.exec();
while (q.next())
    std::printf("%s: %s\n", qPrintable(q.value(0).toString()), qPrintable(q.value(1).toString()));
```

`next()` 移到下一行，`value(列号)` 取出这一列的值（`QVariant`）。也可以用列名 `q.value("body")`，但按列号更快。

`exec()` 失败时返回 `false`，错误信息在 `q.lastError()` 中。**每次 `exec()` 都应该检查返回值**，和数据库专栏中检查 `mysql_query` 的返回值一样。

## 3. 绑定参数，不要拼接 SQL

数据库专栏《用 C 访问 MySQL》中演示过 SQL 注入，在 Qt 中同样会发生。用 `QString::arg` 把用户的输入拼进 SQL：

```cpp
QString name = "O'Brien";
QSqlQuery bad;
bool ok = bad.exec(QString("SELECT count(*) FROM message WHERE sender = '%1'").arg(name));
// ……
QString evil = "x' OR '1'='1";
bad.exec(QString("SELECT count(*) FROM message WHERE sender = '%1'").arg(evil));
```

```
拼接 SQL 查询 O'Brien：失败，near "Brien": syntax error Unable to execute statement
拼接 SQL 查询 x' OR '1'='1：count 为 1000000（整张表）
```

- 名字中有一个单引号，拼出来的 SQL 是 `WHERE sender = 'O'Brien'`，引号提前结束，语法错误。一个正常的用户名就让功能失效了。
- 构造过的输入让条件变成了 `sender = 'x' OR '1'='1'`，永远成立，查出了整张表的 100 万条消息。如果这是一个 `DELETE`，整张表就没了。

用 `prepare` 加 `addBindValue`（或者 `bindValue(":name", ...)` 的命名参数）：

```cpp
QSqlQuery good;
good.prepare("SELECT count(*) FROM message WHERE sender = ?");
good.addBindValue(evil);
good.exec();
```

```
绑定参数查询 x' OR '1'='1：count 为 0
```

参数的值和 SQL 语句是分开传给数据库的，无论参数里有什么字符，都只会被当作一个字符串值，不可能改变语句的结构。

客户端的数据库虽然只有用户自己在用，但写进去的数据很多来自网络：聊天消息的内容、对方的昵称，都是别人能控制的。**凡是 SQL 中的值，一律用绑定参数。** 预处理语句还有一个好处：同一条语句执行多次时，SQL 只需要解析一次（第 4 节的插入就是这样做的）。

## 4. 写入的速度：事务

### 4.1 逐条提交和事务

插入 1000 条消息，一次每条单独提交（SQLite 默认每条语句自动提交），一次把 1000 条放在一个事务中：

```cpp
QSqlQuery ins;
ins.prepare("INSERT INTO message (peer, sender, body, sent_at) VALUES (?, ?, ?, ?)");
auto insertMany = [&](int n, int offset) {
    for (int i = 0; i < n; ++i) {
        ins.addBindValue(QString("user%1").arg((offset + i) % 100));
        ins.addBindValue("lucky");
        ins.addBindValue(QString("第 %1 条消息").arg(offset + i));
        ins.addBindValue(qint64(1'700'000'000'000) + offset + i);
        ins.exec();
    }
};

insertMany(1000, 0);                 // 每条自动提交

db.transaction();
insertMany(1000, 1000);              // 放在一个事务中
db.commit();
```

运行三次：

```
1000 条，每条自动提交：     185.8 ms，每条 0.186 ms
1000 条，放在一个事务中：     1.1 ms，每条 0.001 ms，快 163 倍

1000 条，每条自动提交：     156.1 ms，每条 0.156 ms
1000 条，放在一个事务中：     1.0 ms，每条 0.001 ms，快 163 倍

1000 条，每条自动提交：     140.6 ms，每条 0.141 ms
1000 条，放在一个事务中：     0.9 ms，每条 0.001 ms，快 154 倍
```

快了 150 多倍。原因和数据库专栏《日志与崩溃恢复》中 `innodb_flush_log_at_trx_commit` 的实验一样：**每次提交，数据库都要确保数据真正写到了磁盘上**（调用 `fsync`），否则断电时已经「提交成功」的数据就丢了。`fsync` 要等磁盘确认，比在内存中插入一行慢得多。逐条提交，就是 1000 次 `fsync`；放在一个事务中，只有 1 次。

用这个方法，再插入 99.8 万条，凑够 100 万条消息：

```
再插入 99.8 万条（一个事务）：753 ms，文件 47.1 MB
```

**批量写入时一定要用事务**：从服务器同步下来一批历史消息、导入联系人，都应该放在一个事务中。

### 4.2 一条一条到达的消息：WAL

可是聊天消息是一条一条到达的，每来一条就要保存一条，没法攒成大事务（攒着不写，程序崩溃时就丢了）。这时可以改变 SQLite 的**日志模式**。

SQLite 默认的日志模式是 DELETE（回滚日志）：修改数据前，先把旧的内容复制到一个日志文件中，再修改数据库文件，提交时删除日志文件，每次提交要 `fsync` 好几次。**WAL**（Write-Ahead Logging，预写日志）模式下，修改先追加到一个单独的 `-wal` 文件末尾，攒到一定量再写回数据库文件，提交只需要对 `-wal` 文件 `fsync` 一次，而且是顺序写。这和 MySQL 的 redo log 是同一个思路（数据库专栏《日志与崩溃恢复》）。

另一个设置是**同步级别**：`synchronous=FULL`（默认）每次提交都 `fsync`；`synchronous=NORMAL` 在 WAL 模式下只在把 WAL 写回数据库文件时才 `fsync`。断电时可能丢失最近提交的几个事务，但数据库文件不会损坏。

每种配置用一个新的数据库文件，逐条提交 1000 条，再用一个新的连接重新打开文件，确认数据确实写进去了：

```cpp
for (const QString &p : m.pragmas) {
    QSqlQuery q(db);
    q.exec(p);
    q.finish();                       // 结束这条语句，不留下未完成的读操作
}
```

```
默认（DELETE 日志，FULL 同步） 1000 条逐条提交：  139.7 ms，每条 0.140 ms
                               重新打开后有 1000 行
WAL 日志，FULL 同步           1000 条逐条提交：   30.8 ms，每条 0.031 ms
                               重新打开后有 1000 行
WAL 日志，NORMAL 同步         1000 条逐条提交：    6.9 ms，每条 0.007 ms
                               重新打开后有 1000 行
```

运行两次，结果几乎相同。WAL 让逐条提交快了 4.5 倍，再降低同步级别又快了 4.5 倍，总共 20 倍。客户端保存聊天记录，通常用 WAL 加 NORMAL：丢失断电前最后几条消息可以接受（服务器上还有），数据库损坏则不能接受。WAL 还有一个好处：读和写可以同时进行，界面线程读取聊天记录时，不会被后台线程写入新消息挡住。

<figure class="diagram">
<svg viewBox="0 0 700 206" role="img" aria-label="插入 1000 条消息的耗时：每条自动提交 185.8 毫秒，放在一个事务中只要 1.1 毫秒；逐条提交时，默认的 DELETE 日志加 FULL 同步 139.7 毫秒，WAL 加 FULL 30.8 毫秒，WAL 加 NORMAL 6.9 毫秒">
  <defs></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="238" y="31" text-anchor="end" style="fill:var(--fg);font-size:12px">1000 条，每条自动提交</text><rect x="250" y="18" width="362.3" height="18" style="fill:var(--danger);opacity:.75"/><text x="620.31" y="32" text-anchor="start" style="fill:var(--danger);font-size:11px">185.8 ms</text><text x="238" y="65" text-anchor="end" style="fill:var(--fg);font-size:12px">1000 条，放在一个事务中</text><rect x="250" y="52" width="2.1" height="18" style="fill:var(--accent);opacity:.75"/><text x="260.145" y="66" text-anchor="start" style="fill:var(--accent);font-size:11px">1.1 ms</text><text x="238" y="113" text-anchor="end" style="fill:var(--fg);font-size:12px">逐条提交：DELETE 日志 + FULL</text><rect x="250" y="100" width="272.4" height="18" style="fill:var(--danger);opacity:.75"/><text x="530.415" y="114" text-anchor="start" style="fill:var(--danger);font-size:11px">139.7 ms</text><text x="238" y="147" text-anchor="end" style="fill:var(--fg);font-size:12px">逐条提交：WAL + FULL</text><rect x="250" y="134" width="60.1" height="18" style="fill:var(--warn);opacity:.75"/><text x="318.06" y="148" text-anchor="start" style="fill:var(--warn);font-size:11px">30.8 ms</text><text x="238" y="181" text-anchor="end" style="fill:var(--fg);font-size:12px">逐条提交：WAL + NORMAL</text><rect x="250" y="168" width="13.5" height="18" style="fill:var(--accent);opacity:.75"/><text x="271.455" y="182" text-anchor="start" style="fill:var(--accent);font-size:11px">6.9 ms</text><line x1="20" y1="88" x2="680" y2="88" style="stroke:var(--border);stroke-dasharray:4 4"/>
  </g>
</svg>
</figure>

WAL 模式会保存在数据库文件中，设置一次以后一直有效；同步级别不会保存，每次打开连接都要重新设置：

```cpp
QSqlQuery q;
q.exec("PRAGMA journal_mode=WAL");
q.finish();
q.exec("PRAGMA synchronous=NORMAL");
```

### 4.3 一个坑：没有结束的查询

上面的代码中，执行完 PRAGMA 后调用了 `q.finish()`。第一次做这个实验时没有这一行，而是这样写的：

```cpp
QSqlQuery p("PRAGMA journal_mode=WAL", db);          // 有一行结果 "wal"，没有读取，也没有 finish
// ……接着逐条插入 1000 条
```

测出来逐条提交只要 0.6 毫秒，比 WAL 加 NORMAL 还快 10 倍，不合常理。在插入之后，用另一个连接去读这个数据库：

```
PRAGMA 的查询未结束时，1000 条：0.6 ms
这时另一个连接：查询失败，database is locked Unable to execute statement
p.finish() 之后，另一个连接看到 1000 行
```

`PRAGMA journal_mode=WAL` 会返回一行结果，`p` 没有读取它，也还没有析构，这条语句就一直处于「执行中」的状态，SQLite 为它保持着一个打开的事务。之后的 1000 次插入都落进了这个事务，**一次都没有真正提交**，所以才那么快；数据库也一直被锁着，另一个连接无法读取。直到 `p.finish()`，事务才结束，1000 行才写进文件。如果程序在这之前崩溃，1000 条消息全部丢失。

所以：**返回结果的查询，读完或者不需要了，就调用 `finish()`**（或者让 `QSqlQuery` 尽早离开作用域）。这个坑在执行 `SELECT` 后只读了第一行的代码中也会出现。

## 5. 索引

聊天窗口打开一个会话时，要查出这个会话最近的 50 条消息。在 100 万条消息中查，没有索引和有索引各执行 5 次，取最快的一次：

```cpp
QSqlQuery plan;
plan.exec("EXPLAIN QUERY PLAN SELECT * FROM message WHERE peer = 'user42' ORDER BY sent_at DESC LIMIT 50");
// ……
q.prepare("SELECT sender, body FROM message WHERE peer = ? ORDER BY sent_at DESC LIMIT 50");
```

```
没有索引：  21.95 ms，50 行，查询计划：SCAN message
建索引用了 188 ms
有索引  ：   0.01 ms，50 行，查询计划：SEARCH message USING INDEX idx_peer_time (peer=?)
```

```sql
CREATE INDEX idx_peer_time ON message (peer, sent_at)
```

- 没有索引时，查询计划是 `SCAN message`：扫描整张表的 100 万行，找出 `user42` 的 1 万条，再排序取前 50 条，22 毫秒。
- 建了 `(peer, sent_at)` 的联合索引后，计划变成了 `SEARCH ... USING INDEX`：在索引中直接找到 `user42` 的部分，这部分已经按 `sent_at` 排好了序，从末尾倒着取 50 条就行，0.01 毫秒。

这和数据库专栏《联合索引与索引失效》讲的完全一样：等值条件的列（`peer`）在前，排序的列（`sent_at`）在后，索引同时满足了查找和排序。22 毫秒看起来不多，但它是在界面线程中执行的，用户每切换一次会话就卡一下；数据再多十倍，就是 0.2 秒。SQLite 中同样用 `EXPLAIN QUERY PLAN` 检查查询有没有用上索引，相当于 MySQL 的 `EXPLAIN`。

## 6. 连接与线程

第三章说过，耗时的工作应该放到其他线程。数据库操作能不能放到工作线程中？可以，但**连接不能跨线程使用**。在工作线程中使用主线程创建的默认连接：

```cpp
QtConcurrent::run([] {
    QSqlQuery q;                     // 使用默认连接，它是在主线程中创建的
    bool ok = q.exec("SELECT count(*) FROM message");
    std::printf("工作线程使用主线程的连接：exec 返回 %d，%s\n", ok, qPrintable(q.lastError().text()));

    QSqlDatabase own = QSqlDatabase::addDatabase("QSQLITE", "worker");   // 工作线程自己的连接
    own.setDatabaseName("chat.db");
    own.open();
    QSqlQuery q2(own);
    q2.exec("SELECT count(*) FROM message");
    q2.next();
    std::printf("工作线程使用自己的连接：%d 行\n", q2.value(0).toInt());
});
```

```
qt.sql.qsqldatabase: QSqlDatabasePrivate::database: requested database does not belong to the calling thread.
qt.sql.qsqlquery: QSqlQuery::exec: database not open
工作线程使用主线程的连接：exec 返回 0，Driver not loaded Driver not loaded
工作线程使用自己的连接：1000000 行
```

Qt 检查到连接不属于当前线程，拒绝执行（错误信息「Driver not loaded」有点误导，实际原因是前面那行警告）。每个线程要用 `addDatabase(驱动, 连接名)` 创建**自己的**连接，连接名不能重复。这和 `QObject` 的线程归属是同样的规则：一个连接属于创建它的线程。

常见的安排是：界面线程有一个连接，用于读取（显示聊天记录）；一个专门的工作线程有另一个连接，用于写入（保存收到的消息）。WAL 模式下两者可以同时进行。

## 7. 数据库文件放在哪里

实验中把数据库放在了当前目录，实际的程序不能这样做：当前目录可能是任何地方，甚至没有写权限。应该放在系统为每个应用准备的数据目录中：

```cpp
#include <QStandardPaths>
QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
QDir().mkpath(dir);
db.setDatabaseName(dir + "/chat.db");
```

```
推荐的数据库位置：/Users/lucky/Library/Application Support/sqlite
```

最后一级目录是程序的名字（实验程序叫 `sqlite`），在 `main` 中用 `QCoreApplication::setOrganizationName` 和 `setApplicationName` 设置后，会变成对应的名字。这个目录在 Windows 上是 `C:/Users/用户名/AppData/Roaming/...`，在 Linux 上是 `~/.local/share/...`。

## 8. 小结

- SQLite 是嵌入式数据库，整个数据库是一个文件，Qt 自带驱动 `QSQLITE`。`QSqlDatabase` 管理连接，`QSqlQuery` 执行语句、遍历结果；每次 `exec` 都要检查返回值。
- SQL 中的值一律用 `prepare` 加绑定参数。拼接 SQL 时，一个单引号就让语句出错，构造过的输入能查出（或删除）整张表。
- 每次提交都要 `fsync`。批量写入放在一个事务中，实验中快了 150 多倍；逐条写入时打开 WAL 并把同步级别设为 NORMAL，快了约 20 倍。
- 返回结果的查询用完要 `finish()`，否则它会让事务一直开着，后面的写入都不会提交，其他连接被锁住。
- 为常用的查询建合适的索引，用 `EXPLAIN QUERY PLAN` 验证。实验中查一个会话最近的 50 条消息，从 22 毫秒降到 0.01 毫秒。
- 一个连接只能在创建它的线程中使用，每个线程用不同的连接名创建自己的连接。
- 数据库文件放在 `QStandardPaths::AppDataLocation`。

第四章到这里结束：客户端能用 TCP 长连接可靠地收发消息、检测断线并重连，能调用 HTTP 接口，能把数据保存在本地。下一章把前四章的内容组合起来，做一个完整的聊天客户端。

## 练习

1. 给第 2 节的 `message` 表加一个 `status` 列（发送中、已发送、发送失败），实现上一篇第 2.2 节「应用层确认」中「等待确认的消息」的持久化：程序崩溃重启后，还没确认的消息能重新发送。
2. 第 5 节的查询如果改成 `WHERE peer = ? AND body LIKE '%吃饭%'`（在会话中搜索），索引还有用吗？用 `EXPLAIN QUERY PLAN` 验证。SQLite 的 FTS5 全文索引能怎样改进它？
3. 用两个线程同时写同一个数据库（各自的连接），一个线程在一个大事务中插入 10 万条，另一个线程逐条插入，观察会不会出现 `database is locked`。`PRAGMA busy_timeout` 有什么作用？
4. 把 100 万条消息中的 `peer` 从字符串（`user42`）改成整数编号，再单独建一张会话表。数据库文件变小了多少？查询变快了吗？

<details>
<summary>点击查看答案</summary>

1. 表结构加一列：

   ```sql
   CREATE TABLE message (
       id INTEGER PRIMARY KEY,
       client_id INTEGER NOT NULL UNIQUE,      -- 发送时的编号，确认消息中带回来
       peer TEXT NOT NULL,
       sender TEXT NOT NULL,
       body TEXT NOT NULL,
       sent_at INTEGER NOT NULL,
       status INTEGER NOT NULL DEFAULT 0       -- 0 发送中，1 已发送，2 发送失败
   );
   CREATE INDEX idx_pending ON message (status) WHERE status = 0;   -- 只索引「发送中」的行
   ```

   发送和确认的流程：

   ```cpp
   // 发送：先写数据库，再发网络
   q.prepare("INSERT INTO message (client_id, peer, sender, body, sent_at, status) VALUES (?, ?, ?, ?, ?, 0)");
   // ……绑定参数，exec
   conn->sendChat(msg);

   // 收到 ack
   q.prepare("UPDATE message SET status = 1 WHERE client_id = ?");

   // 启动时 / 重连后：把还没确认的消息重新发送
   q.exec("SELECT client_id, peer, body, sent_at FROM message WHERE status = 0 ORDER BY client_id");
   while (q.next())
       conn->sendChat(/* 用原来的 client_id 组装消息 */);
   q.finish();
   ```

   关键是**先写数据库，再发送**：如果先发送、再写数据库，程序在两者之间崩溃，消息已经发出去了，本地却没有记录。先写数据库的话，最坏情况是消息发了两次，由服务器按编号去重（上一篇练习 2）。`client_id` 要在重启后继续递增，不能从 1 重新开始，可以取 `SELECT max(client_id) FROM message` 的结果再加 1。

   「发送中」的消息在全部消息中只占极少数，所以用了一个**部分索引**（`WHERE status = 0`），索引只包含这几行，很小。

2. 在 100 万条消息中实测（会话 `user42` 有 1 万条，其中七分之一包含「一起吃饭」）：

   ```
   最近 50 条:          0.02 ms, SEARCH message USING INDEX idx_peer_time (peer=?)
   会话中搜索 LIMIT 50:   0.07 ms, 50 行, SEARCH message USING INDEX idx_peer_time (peer=?)
   会话中搜索 count:      6.67 ms, SEARCH message USING INDEX idx_peer_time (peer=?)
   全表搜索 count:       39.57 ms, SCAN message
   ```

   索引**仍然有用，但只用了一半**：`peer = ?` 用索引把范围缩小到这个会话的 1 万行，这部分已经按时间排好序；`LIKE '%吃饭%'` 以 `%` 开头，没法用索引，只能对这 1 万行逐行检查正文。

   - 要找最近的 50 条匹配时，因为匹配的消息很多，倒着扫描不了多少行就凑够了，0.07 毫秒。
   - 要把会话中所有匹配的都找出来（count），就要检查全部 1 万行，6.7 毫秒。
   - 在所有会话中搜索，没有任何条件能用索引，扫描整张表 100 万行，40 毫秒。

   搜索的词很少见时（例如「第 123442 条」，只有 1 条匹配），`LIMIT 50` 也要扫描完整个范围：会话中约 8 毫秒，全表约 75 毫秒。

   SQLite 的 **FTS5** 全文索引为正文中的词建立倒排索引（「哪些行包含这个词」）。中文没有空格分词，可以用 `trigram` 分词器，把文字切成连续 3 个字的片段：

   ```sql
   CREATE VIRTUAL TABLE message_fts USING fts5(
       body, content='message', content_rowid='id', tokenize='trigram');
   INSERT INTO message_fts(rowid, body) SELECT id, body FROM message;
   ```

   ```sql
   SELECT m.body FROM message m JOIN message_fts f ON f.rowid = m.id
   WHERE message_fts MATCH '"第 123442 条"' AND m.peer = 'user42'
   ORDER BY m.sent_at DESC LIMIT 50;
   ```

   建索引用了 850 毫秒，数据库文件从 80 MB 增加到 137 MB。搜索少见的词，全表只要 0.4 毫秒，比 `LIKE` 快约 200 倍。但它也有代价和限制：

   - 索引要占不少空间（这里增加了约 70%），插入消息时也要同时更新它（可以用触发器自动完成）。
   - `trigram` 要求搜索的词至少 3 个字符，搜「吃饭」两个字是查不到的，要退回到 `LIKE`。
   - 搜索很常见的词（14 万条都包含「一起吃饭」）时，FTS5 要先找出全部 14 万行，再按会话过滤和排序，反而要 30 毫秒，比在会话中直接 `LIKE` 慢。

   所以对聊天记录来说，在当前会话中搜索可以直接用 `LIKE`（数据量小）；在全部聊天记录中搜索时，才值得建 FTS5 索引。

3. 一个线程在一个事务中插入 10 万条，另一个线程同时逐条插入 300 条，各自用自己的连接。先把 Qt 的等待时间设成 0（`db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=0")`），再用 Qt 的默认设置：

   ```
   busy_timeout = 0：大事务 50 ms，错误 104；逐条插入成功 0、失败 300，单条最长等待 0 ms
   Qt 默认（busy_timeout 5000 ms）：大事务 34 ms，错误 0；逐条插入成功 300、失败 0，单条最长等待 49 ms
   ```

   不等待时，出现了大量的 `database is locked`：SQLite 同一时刻只允许一个连接写入，另一个连接想写时，发现数据库被锁着，立即失败。大事务也有 104 条失败了，是在开始时被逐条插入的那个线程抢到了锁。

   `PRAGMA busy_timeout`（Qt 中是连接选项 `QSQLITE_BUSY_TIMEOUT`）设置**遇到锁时最多等待多久**，在这段时间内 SQLite 会反复重试，锁释放了就继续执行。Qt 的 SQLite 驱动默认设置了 5000 毫秒，所以用默认设置时没有任何错误，逐条插入的线程最多等了 49 毫秒（大事务写完的时间）。

   这说明：多个连接写同一个数据库时，等待的时间一定要比最长的写事务长；写事务应该尽量短，大批量的导入可以拆成每 1000 条一个事务，让别的连接有机会插进来。最根本的办法还是第 6 节说的：只让一个线程负责写入。

4. 改成会话表加整数编号：

   ```sql
   CREATE TABLE conversation (id INTEGER PRIMARY KEY, peer TEXT NOT NULL UNIQUE);
   CREATE TABLE message (id INTEGER PRIMARY KEY, conv_id INTEGER NOT NULL, sender TEXT NOT NULL,
                         body TEXT NOT NULL, sent_at INTEGER NOT NULL);
   CREATE INDEX idx_conv_time ON message (conv_id, sent_at);
   ```

   各插入 100 万条，建好索引，`VACUUM` 之后：

   ```
   peer 是字符串: 文件 80.2 MB，最近 50 条 0.018 ms（50 行）
   peer 改成整数: 文件 70.6 MB，最近 50 条 0.019 ms（50 行）
   ```

   文件小了 9.6 MB（12%）：`user42` 这样的字符串要 6～7 个字节，0～99 的整数在 SQLite 中只占 1 个字节，而且表和索引中各存了一份。查询**没有变快**：有了索引，两种写法都只读几十行，时间都花在固定的开销上；整数版本还要先在会话表中按名字查一次编号。

   规范化（数据库专栏《范式与反范式》）的好处主要不在速度，而在于：会话的名字、头像、置顶状态等信息只存一份，改名时只改一行；消息表变小了，同样的内存能缓存更多的消息。

</details>
