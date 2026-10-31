---
title: 实战：消息确认与本地存储
date: 2026-10-31 11:00:00
updated: 2026-10-31 11:00:00
categories:
  - 客户端开发
  - 综合实战
tags:
  - Qt
  - C++
  - SQLite
  - 可靠性
order: 3
description: 一条消息从按下回车到对方收到，中间可能遇到服务器卡住、连接断开、客户端崩溃。QtChat 的做法是：先写进 SQLite，状态是「发送中」，再发给服务器；收到 ack 才改成「已发送」；每次登录成功都把所有「发送中」的消息重发一遍，靠服务器去重。实测：用 docker pause 冻结服务器，发出的两条消息变成浅色的「发送中…」，15 秒后心跳超时重连，服务器恢复后它们变成正常的蓝色，对方各收到一次；消息还在「发送中」时客户端被 kill -9，重启后 0.4 秒就补发了；连上一轮实验中遗留的一条未确认消息，也在这次登录时被送达。服务器把同一条消息发了两遍，界面上只显示一次。还验证了为什么编号只用 52 位：2^53+1 经过一次 JSON 往返变成了 2^53。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：看懂 QtChat 中一条消息的完整生命周期，理解「先存后发、确认后改状态、登录后重发、两端去重」怎样保证消息不丢、不重；理解为什么消息要先写进数据库；会用 `INSERT OR IGNORE` 和唯一索引实现接收端的去重；会设计实验验证可靠性：冻结服务器、杀掉客户端、伪造重复的消息。
>
> **前置知识**：本章前两篇；第四章《断线检测与重连》（应用层确认）、《SQLite 本地存储》（WAL、唯一索引、`finish()`）。
>
> **实验环境**：服务器在 Docker 容器中，用 `docker pause` / `docker unpause` 冻结和恢复它的所有进程（相当于第四章的 `SIGSTOP` / `SIGCONT`）。客户端按脚本自动操作（上一篇第 6.3 节），对方有时用一个 Python 脚本代替，以便记录收到消息的时刻。

## 1. 一条消息的生命周期

```
按下回车
  │
  ▼
MessageStore::addOutgoing ── 写进 SQLite，状态 = 发送中（0），生成 52 位随机编号
  │
  ├─► 界面立即显示（浅色气泡，「发送中…」）
  │
  ▼
已登录？──否──► 先不发，等登录
  │是
  ▼
Connection::send ── 写进套接字（不代表对方收到）
  │
  ▼
收到 ack {id} ──► 状态 = 已发送（1），气泡变成正常颜色
收到 error {id} ─► 状态 = 失败（2），显示红色的「发送失败」

断线、重连、登录成功 ──► 把所有「发送中」的消息按顺序重发一遍
客户端崩溃、重启、登录成功 ──► 同上（状态保存在数据库中）
```

### 1.1 先存后发

```cpp
void ChatClient::sendText(const QString &peer, const QString &text)
{
    ChatMessage m = m_store.addOutgoing(peer, text);      // 先存进数据库，状态是「发送中」
    emit messageAdded(m);
    if (m_loggedIn)
        sendFrame(m);                                      // 没有登录时先不发，登录后统一补发
}
```

**先写数据库，再发网络**，顺序不能反过来。如果先发、后存，发出去之后、写数据库之前程序崩溃了，这条消息就从本地消失了：对方可能收到了，自己却没有记录；或者对方也没收到，用户以为自己发过了。先存的话，最坏的情况是存了、还没发，重启后补发即可。

没有登录（断线中、正在重连）时不立即发送，统一等登录成功后从数据库中取出补发。这样「断线期间写的消息」和「断线前发出、没有确认的消息」走的是同一条路径，第四章第 2 篇中的 `m_pending` 列表就不需要了：**数据库就是待发送的队列**，而且程序退出也不会丢。

### 1.2 存储

```cpp
ChatMessage MessageStore::addOutgoing(const QString &peer, const QString &body)
{
    ChatMessage m;
    m.peer = peer;
    m.outgoing = true;
    // 随机的 52 位编号：JSON 的数字是双精度浮点数，2^53 以内的整数才能精确表示
    m.msgId = QRandomGenerator::global()->bounded(qint64(1) << 52);
    m.body = body;
    m.ts = QDateTime::currentMSecsSinceEpoch();
    m.status = ChatMessage::Sending;
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO message (peer, outgoing, msg_id, body, ts, status) VALUES (?, 1, ?, ?, ?, ?)");
    q.addBindValue(peer);
    q.addBindValue(m.msgId);
    q.addBindValue(body);
    q.addBindValue(m.ts);
    q.addBindValue(int(m.status));
    q.exec();
    m.rowId = q.lastInsertId().toLongLong();
    return m;
}
```

数据库在 `open` 时设置成 WAL 加 `synchronous=NORMAL`（第四章《SQLite 本地存储》第 4.2 节），每条消息单独提交也只要几微秒；表上有两个索引：

```cpp
q.exec("PRAGMA journal_mode=WAL");
q.finish();                                   // 返回结果的语句用完要 finish
q.exec("PRAGMA synchronous=NORMAL");
// ……建表
// 同一个人发来的同一条消息只存一次；按会话和时间查最近的消息
q.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_msg ON message (peer, outgoing, msg_id)");
q.exec("CREATE INDEX IF NOT EXISTS idx_peer_ts ON message (peer, ts)");
```

`q.finish()` 那一行，就是第四章 4.4 篇第 4.3 节踩过的坑：不结束 `PRAGMA journal_mode` 的查询，后面的写入都不会提交。

每个用户一个数据库文件，放在 `QStandardPaths::AppDataLocation`（第四章 4.4 篇第 7 节），在实验的机器上是 `~/Library/Application Support/lucky-notes/QtChat/lucky.db`。

### 1.3 确认和补发

```cpp
void ChatClient::onMessage(const QJsonObject &msg)
{
    const QString type = msg["type"].toString();
    if (type == "login_ok") {
        m_loggedIn = true;
        // ……更新联系人
        // 补发所有还没有被确认的消息：断线前发出、但不知道对方收到没有的，以及断线期间写的
        // 服务器按编号去重，重复发送不会让对方收到两次
        for (const ChatMessage &m : m_store.pending())
            sendFrame(m);
    } else if (type == "ack") {
        qint64 id = qint64(msg["id"].toDouble());
        m_store.setStatus(id, ChatMessage::Sent);
        emit statusChanged(id, ChatMessage::Sent);
    } else if (type == "error") {
        // ……标记为失败
    }
    // ……
}
```

`pending()` 按插入的顺序（`ORDER BY id`）取出，补发的顺序和用户发送的顺序一致。`statusChanged` 信号连接到 `MessageModel::setStatus`，它找到这条消息，发出 `dataChanged`，委托就把气泡重画成正常的颜色（上一篇第 3.3 节）。

### 1.4 编号为什么是 52 位

协议中的编号是 JSON 中的一个数字。把几个整数用 Qt 生成 JSON、再解析回来：

```cpp
for (qint64 id : {qint64(1) << 52, (qint64(1) << 53) + 1, qint64(1793445574705123456)}) {
    QJsonObject o{{"id", id}};
    QByteArray text = QJsonDocument(o).toJson(QJsonDocument::Compact);
    qint64 back = qint64(QJsonDocument::fromJson(text)["id"].toDouble());
    std::printf("%20lld → %-32s → %20lld  %s\n", id, text.constData(), back, back == id ? "不变" : "变了！");
}
```

```
    4503599627370496 → {"id":4503599627370496}          →     4503599627370496  不变
    9007199254740993 → {"id":9007199254740993}          →     9007199254740992  变了！
 1793445574705123456 → {"id":1793445574705123456}       →  1793445574705123328  变了！
```

服务器用的 cJSON 也一样：

```
cJSON 解析 9007199254740993 得到 9007199254740992
```

生成的 JSON 文本是对的，但**解析时数字被转成了 `double`**，双精度浮点数只有 53 位有效数字，超过 2^53 的整数就会被舍入。2^53 + 1 变成了 2^53，一个用纳秒时间戳拼出来的 19 位编号，末尾也被改了。如果编号被改了，`ack` 中的编号就对不上本地的消息，这条消息会永远显示「发送中」，每次登录都重发一遍。

所以编号控制在 2^52 以内。需要更大的编号时（比如雪花算法生成的 64 位 ID），协议中应该用字符串表示它，很多大公司的 API 都是这样做的。

## 2. 实验一：服务器卡住

lucky 和 zhangsan 正在聊天，第 4 秒用 `docker pause` 冻结服务器，lucky 接着发了两条消息；第 25 秒恢复服务器。

冻结后 lucky 发的「在吗？」「怎么不回我」：

<img src="/img/client/chat-sending.png" alt="服务器冻结期间发出的两条消息：浅色气泡，显示「发送中…」" width="640">

两条消息是浅色的，下面显示「发送中…」。状态栏还是「已连接」：心跳要 15 秒没有回应才判定断线，在这之前客户端并不知道服务器出了问题。

心跳超时后，客户端断开重连。服务器这时还冻结着，可是重连「成功」了：

<img src="/img/client/chat-relogin.png" alt="重连后状态栏停在「正在登录…」，新消息仍然是「发送中」" width="640">

（这张截图来自另一次运行，所以消息不同。）状态栏停在「正在登录…」：TCP 连接建立了（Docker 的端口转发替冻结的容器接受了连接，和第四章 4.2 篇中内核替暂停的进程完成握手是同一类现象），但服务器没有回复 `login_ok`。这时客户端不会发送任何消息，等登录成功。

服务器恢复后：

<img src="/img/client/chat-sent.png" alt="服务器恢复后，两条消息变成了正常的蓝色" width="640">

<img src="/img/client/chat-zhangsan-after.png" alt="zhangsan 那边，两条消息各收到一次" width="640">

两条消息变成了正常的蓝色，zhangsan 那边各收到一次。服务器的日志：

```
[45607] lucky 上线
[45632] zhangsan 上线
[49156] lucky -> zhangsan          ← 冻结前的「收到请回复」
[70017] lucky 下线                 ← 恢复后，处理积压的事件：旧连接已经被客户端关掉了
[70017] zhangsan 下线
[70017] zhangsan 上线               ← 两个客户端的新连接登录
[70017] lucky 上线
[70019] lucky -> zhangsan          ← 补发的「在吗？」
[70019] lucky -> zhangsan          ← 补发的「怎么不回我」
```

冻结期间写进旧连接的两条消息，因为旧连接已经被客户端 `abort()` 了，服务器根本没有处理它们；新连接登录后，客户端补发，服务器各转发一次。

## 3. 实验二：客户端崩溃

这一次让服务器冻结时，lucky 发出一条消息，然后用 `kill -9` 杀掉 lucky（模拟崩溃、断电、被用户强制退出），再恢复服务器，重新启动 lucky。zhangsan 换成一个 Python 脚本，打印收到每条消息的时刻：

```
第 3.5 秒：服务器冻结
第 6.5 秒：kill -9 lucky
第 7.5 秒：服务器恢复
第 10.5 秒：重新启动 lucky
zhangsan 第  1.2 秒收到：到家了吗？（id=106833766255317）
zhangsan 第 10.9 秒收到：临走前发的消息（id=667169616275468）
```

杀掉之前，消息停在「发送中」：

<img src="/img/client/chat-before-kill.png" alt="kill -9 之前：最后一条消息是「发送中…」" width="640">

重启后 0.4 秒，「临走前发的消息」送达，只收到一次。数据库中它的状态也更新了：

```bash
sqlite3 lucky.db "SELECT body, status FROM message WHERE body LIKE '临走%'"
```

```
临走前发的消息|1
```

还有一个计划之外的结果：**zhangsan 在第 1.2 秒收到了「到家了吗？」**。这是第 2 节那张「正在登录…」截图的那次运行中发出的消息：那次运行在服务器恢复之前就结束了，消息一直是「发送中」，留在了数据库中。这次 lucky 一启动、一登录，它就被补发了。它跨越了两次程序退出，最终还是送到了。

这正是「先存后发」的价值：状态在数据库中，而不是在内存中，程序怎么结束都不会丢。

## 4. 实验三：收到重复的消息

服务器按编号去重，保证了「重发」不会让对方收到两次。但接收端也应该防一手：服务器补发离线消息、或者将来改成多台服务器时，同一条消息有可能被投递两次。

`addIncoming` 用 `INSERT OR IGNORE` 加唯一索引 `(peer, outgoing, msg_id)`：

```cpp
bool MessageStore::addIncoming(ChatMessage &m)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT OR IGNORE INTO message (peer, outgoing, msg_id, body, ts, status) VALUES (?, 0, ?, ?, ?, ?)");
    // ……绑定参数
    q.exec();
    if (q.numRowsAffected() == 0)
        return false;                             // 唯一索引冲突：这条消息已经收到过了
    m.rowId = q.lastInsertId().toLongLong();
    m.status = ChatMessage::Received;
    return true;
}
```

```cpp
} else if (type == "msg") {
    // ……从 JSON 取出各个字段
    if (m_store.addIncoming(m))                        // 重复收到的消息不再显示
        emit messageAdded(m);
}
```

`INSERT OR IGNORE` 遇到唯一索引冲突时不报错，只是不插入，`numRowsAffected()` 为 0。去重的判断交给了数据库，而且是持久的：即使重复的消息在程序重启之后才到，也能被识别出来。

用一个假的服务器测试：登录后，把编号 42 的消息连发两遍，再发一条编号 43 的：

```python
m = {"type": "msg", "from": "dup_test", "id": 42, "text": "这条消息服务器发了两遍", "ts": ...}
send(c, m); send(c, m)
send(c, {"type": "msg", "from": "dup_test", "id": 43, "text": "这是另一条", "ts": ...})
```

<img src="/img/client/chat-dedup.png" alt="编号 42 的消息只显示了一次" width="640">

```
42|这条消息服务器发了两遍
43|这是另一条
```

界面上只显示了一次，数据库中也只有一行。

## 5. 还没有处理的情况

QtChat 的可靠性设计覆盖了网络中断、服务器卡死、客户端崩溃、重复投递。但它还有一些简化，真实的聊天软件需要进一步处理：

- **服务器重启**：用户表和离线消息只在内存中，服务器重启后，离线消息丢失，之前「已发送」的消息其实没送到。服务器也要「先存后确认」：把消息写进数据库后才回复 `ack`。
- **永远发不出去的消息**：如果对方不存在，服务器回复 `error`，状态变成「失败」；但如果服务器一直连不上，消息会永远「发送中」。可以在界面上提供「重发」和「删除」，或者超过一段时间自动标记为失败。
- **消息的顺序**：补发时按本地的发送顺序，但两个人同时发消息时，双方看到的顺序可能不同（各自按本地时间排序）。可以用服务器的时间戳 `ts` 统一排序。
- **多台设备**：同一个账号在两台电脑上，一台发的消息另一台看不到（服务器只转发给对方），需要服务器保存完整的聊天记录，客户端登录时同步。

## 6. 小结

- 消息先写进数据库（状态「发送中」），再发给服务器；收到 `ack` 改为「已发送」，收到 `error` 改为「失败」。
- 数据库就是待发送队列：每次登录成功，把所有「发送中」的消息按顺序重发。断线、重连、崩溃、重启都走这一条路径。
- 发送端由服务器按编号去重，接收端用唯一索引加 `INSERT OR IGNORE` 去重，两边合起来实现「不丢、不重」。
- JSON 解析时数字会变成双精度浮点数，超过 2^53 的整数不精确。编号控制在 52 位，或者用字符串。
- 用 `docker pause`、`kill -9`、伪造的服务器，可以重现那些平时很难遇到的故障，验证设计是否真的有效。

## 练习

1. 实现「发送失败」的消息右键菜单：重发、删除。重发时应该用原来的编号还是新的编号？为什么？
2. 让发送中超过 2 分钟的消息自动变成「失败」。重启程序后，这个计时应该从什么时候开始算？
3. 修改服务器，让它在把消息写进 SQLite 后才回复 `ack`，并在启动时恢复用户表和离线消息。重做实验二，在服务器冻结时用 `docker kill` 杀掉服务器再启动，消息还能送到吗？
4. 现在客户端按本地时间 `ts` 排序。两个客户端的系统时间相差 5 分钟时会出现什么问题？改用服务器 `ack` 中的 `ts` 排序，`MessageStore` 需要怎样修改？
