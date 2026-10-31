---
title: 实战：聊天协议与服务器
date: 2026-10-31 09:00:00
updated: 2026-10-31 09:00:00
categories:
  - 客户端开发
  - 综合实战
tags:
  - Qt
  - C
  - epoll
  - 协议设计
order: 1
description: 最后一章把前四章组合成一个能用的聊天程序 QtChat。这一篇先设计协议、写服务器：消息格式沿用第四章的「4 字节长度 + JSON」，定义登录、在线状态、聊天、确认、心跳、被顶下线等 8 种消息；服务器在网络专栏 epoll 回声服务器的基础上改写，用 cJSON 解析，在 Docker 的 Ubuntu 中运行。用一个 Python 脚本逐项验证：同一个编号的消息重发一次，服务器只回确认、不再转发；对方不在线时消息被存起来，上线时跟着登录结果一起补发；同一个账号在别处登录，旧连接先收到 kicked 再被关闭。设计中还要处理一个容易忽略的问题：两个开着自动重连的客户端用同一个账号登录，会无休止地互相踢下线。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：为聊天程序设计一个简单的应用层协议，理解每种消息的作用；理解消息编号、确认、去重怎样配合，实现「至少一次投递」加「去重」；理解离线消息和重复登录的处理；看懂在 epoll 回声服务器的基础上，聊天服务器增加了哪些东西；会用脚本测试一个协议。
>
> **前置知识**：本专栏第四章（分帧、心跳与重连、应用层确认）；计算机网络专栏《IO 多路复用》（epoll 回声服务器）。
>
> **实验环境**：服务器在 Docker 的 Ubuntu 24.04 容器中运行（GCC 13.3，cJSON），端口 9000 映射到本机；客户端在 macOS 27.0 上，Qt 6.12.0。完整的源码见第 5 节的下载。

## 1. 要做什么

这一章做一个聊天程序 **QtChat**，把前四章的内容用起来：

| 功能 | 用到的内容 |
|---|---|
| 登录对话框、记住上次的输入 | 第二章《对话框与输入校验》 |
| 联系人列表、聊天气泡 | 第二章《Model/View》《自定义控件与绘图》 |
| 回车发送、Shift+回车换行 | 第三章《事件的分发与传播》（事件过滤器） |
| 收发消息、断线自动重连 | 第四章《QTcpSocket 与消息分帧》《断线检测与重连》 |
| 消息的「发送中」状态、重连后补发 | 第四章《断线检测与重连》第 2.2 节（应用层确认） |
| 聊天记录保存在本地 | 第四章《SQLite 本地存储》 |
| 打包成别人能直接运行的程序 | 本章第 4 篇 |

完成后的样子（用户 lucky 的窗口，和 zhangsan 的客户端运行在同一台电脑上）：

<img src="/img/client/chat-lucky.png" alt="lucky 的 QtChat 窗口：联系人列表和与 zhangsan 的对话" width="640">

本章分四篇：这一篇设计协议、写服务器；第 2 篇写客户端的界面；第 3 篇处理消息的确认、补发和本地存储；第 4 篇打包发布。

## 2. 协议设计

### 2.1 消息格式

沿用第四章的格式：**4 字节大端的长度 + UTF-8 的 JSON 正文**。每条消息都有一个 `type` 字段说明它是什么。

客户端发给服务器的：

| type | 字段 | 含义 |
|---|---|---|
| `login` | `user` | 登录 |
| `msg` | `id`、`to`、`text` | 发一条聊天消息给 `to` |
| `ping` | | 心跳 |

服务器发给客户端的：

| type | 字段 | 含义 |
|---|---|---|
| `login_ok` | `user`、`users`（其他用户和在线状态） | 登录成功 |
| `presence` | `user`、`online` | 某个用户上线或下线了 |
| `msg` | `from`、`id`、`text`、`ts` | 别人发来的一条消息，`ts` 是服务器收到它的时间 |
| `ack` | `id`、`ts` | 你发的编号为 `id` 的消息，服务器已经处理了 |
| `error` | `id`、`reason` | 你发的消息处理失败了，例如对方不存在 |
| `pong` | | 心跳回复 |
| `kicked` | | 你的账号在别处登录了，这个连接要被关闭 |

一个典型的过程：

```
lucky                         服务器                        zhangsan
  │ login {user:lucky}          │                              │
  │ ──────────────────────────► │                              │
  │ login_ok {users:[...]}      │                              │
  │ ◄────────────────────────── │                              │
  │                             │  presence {lucky, online}    │
  │                             │ ───────────────────────────► │
  │ msg {id:7, to:zhangsan,     │                              │
  │      text:"你好"}            │                              │
  │ ──────────────────────────► │  msg {from:lucky, id:7,      │
  │                             │       text:"你好", ts:...}    │
  │                             │ ───────────────────────────► │
  │ ack {id:7, ts:...}          │                              │
  │ ◄────────────────────────── │                              │
```

为了把注意力放在客户端上，这个协议做了大量的简化：没有密码（任何人可以用任何名字登录），没有加密（明文传输），用户和离线消息只保存在服务器的内存中（服务器重启就没了），没有群聊。真实的聊天服务至少要有密码或令牌认证、TLS 加密（网络专栏《HTTPS 与 TLS》）、持久化的存储。

### 2.2 消息编号、确认与去重

第四章第 2.2 篇的实验说明：`write` 成功不代表对方收到，判定断线之前写进旧连接的消息可能丢失。所以协议采用这样的办法：

1. **客户端给每条消息一个编号 `id`**，先存进本地数据库，状态是「发送中」，再发给服务器；
2. **服务器处理完，回复 `ack`**，客户端把这条消息标记为「已发送」；
3. **重连并登录成功后，客户端把所有还是「发送中」的消息重新发一遍**：包括断线期间写的，也包括断线前发出、但没有收到 `ack` 的；
4. 于是同一条消息可能被服务器收到两次（第一次处理了，`ack` 在路上丢了），**服务器按发送者和编号去重**：已经处理过的编号，只回复 `ack`，不再转发。

这叫作「**至少一次投递 + 去重**」：重发保证消息不丢，去重保证对方不会收到两遍。几乎所有可靠的消息系统都是这样做的。

编号怎样生成？最容易想到的是用本地数据库的自增主键，可是同一个用户可能在两台电脑上登录，两台电脑的数据库各自从 1 开始编号，服务器就会把另一台电脑的第 1 条消息当成重复的扔掉。QtChat 用一个 **52 位的随机数**：两条消息碰巧相同的概率可以忽略。为什么是 52 位而不是 64 位？因为 JSON 中的数字通常按双精度浮点数处理，双精度只能精确表示 2^53 以内的整数，更大的整数会被悄悄改变（C 语言专栏《数据类型与变量》中的浮点数精度）。

### 2.3 离线消息与重复登录

**对方不在线**：服务器把消息存在对方名下，对方登录时，紧跟在 `login_ok` 后面补发。

**同一个账号在两个地方登录**：比较简单的做法是让新的登录成功，把旧的连接关掉，「顶下线」。但这里有一个陷阱：客户端都有自动重连（第四章第 2 篇）。旧的客户端被关掉连接后，会自动重连、重新登录，又把新的顶下去；新的再重连……两个客户端会无休止地互相踢下线。

所以服务器在关闭旧连接之前，先发一条 `kicked`，客户端收到后**停止自动重连**，提示用户「你的账号在别处登录了」。这就是很多聊天软件中「你的账号已在另一台设备登录」的由来。

## 3. 服务器

### 3.1 在回声服务器的基础上

服务器 `chatd.c` 约 330 行，以网络专栏《IO 多路复用》中的 `echo_epoll.c` 为基础：同样是一个线程、一个 epoll 主循环、所有的套接字都是非阻塞的。增加的部分：

| 回声服务器 | 聊天服务器 |
|---|---|
| 每个连接一个固定 4 KB 的 `pending` | 每个连接一个可以增长的接收缓冲区 `in` 和发送缓冲区 `out` |
| 读到什么就写回什么 | 按「长度 + JSON」切出完整的消息，按 `type` 分别处理 |
| 连接之间互不相关 | 用户表：用户名、当前的连接、最近收到的消息编号、离线消息 |

数据结构：

```c
struct buf {                     // 可增长的字节缓冲区
    char *data;
    size_t len, cap;
};

struct conn {
    int fd;
    struct buf in;               // 收到、还没切成消息的字节
    struct buf out;              // 要发、还没写出去的字节
    struct user *user;           // 登录后指向用户，NULL 表示还没登录
};

struct user {
    char name[32];
    struct conn *conn;           // 在线时指向它的连接
    long long seen[SEEN];        // 最近收到的消息编号（环形）
    int seen_pos;
    struct buf offline;          // 不在线时收到的消息，已经编码成帧
};
```

### 3.2 读取和分帧

```c
// 读出数据，切出所有完整的消息；返回 -1 表示要关闭连接
static int on_readable(struct conn *c)
{
    char tmp[65536];
    for (;;) {
        ssize_t r = read(c->fd, tmp, sizeof tmp);
        if (r == 0) return -1;                   // 对方关闭
        if (r == -1) {
            if (errno == EAGAIN) break;
            return -1;
        }
        buf_append(&c->in, tmp, r);
    }
    while (c->in.len >= 4) {
        uint32_t len;
        memcpy(&len, c->in.data, 4);
        len = ntohl(len);
        if (len > MAX_FRAME) return -1;          // 长度不合理，断开
        if (c->in.len < 4 + len) break;          // 还没收全
        on_frame(c, c->in.data + 4, len);
        buf_consume(&c->in, 4 + len);
    }
    return 0;
}
```

和第四章客户端的 `MessageReader` 是同一个逻辑，只是换成了 C：先把数据读到 `EAGAIN` 为止，追加到缓冲区；再循环切出所有完整的消息；长度超过 1 MB 就断开。`memcpy` 到一个 `uint32_t` 再 `ntohl`，而不是直接把 `c->in.data` 强制转换成 `uint32_t *`：缓冲区中的位置不一定对齐到 4 字节，未对齐的访问在一些平台上会出错（C 语言专栏《结构体、联合与枚举》中的内存对齐）。

### 3.3 处理聊天消息

```c
static void on_chat(struct conn *c, cJSON *msg)
{
    cJSON *id = cJSON_GetObjectItem(msg, "id");
    cJSON *to = cJSON_GetObjectItem(msg, "to");
    cJSON *text = cJSON_GetObjectItem(msg, "text");
    if (!c->user || !cJSON_IsNumber(id) || !cJSON_IsString(to) || !cJSON_IsString(text))
        return;
    struct user *peer = find_user(to->valuestring, 0);
    long long ts = now_ms();
    cJSON *reply = cJSON_CreateObject();
    cJSON_AddNumberToObject(reply, "id", id->valuedouble);
    if (!peer) {
        cJSON_AddStringToObject(reply, "type", "error");
        cJSON_AddStringToObject(reply, "reason", "没有这个用户");
    } else {
        if (seen_before(c->user, (long long)id->valuedouble)) {
            log_msg("%s 重发的消息，已经处理过，只回复确认%s", c->user->name, "");
        } else {
            cJSON *fwd = cJSON_CreateObject();
            cJSON_AddStringToObject(fwd, "type", "msg");
            cJSON_AddStringToObject(fwd, "from", c->user->name);
            cJSON_AddNumberToObject(fwd, "id", id->valuedouble);
            cJSON_AddStringToObject(fwd, "text", text->valuestring);
            cJSON_AddNumberToObject(fwd, "ts", (double)ts);
            if (peer->conn)
                send_json(peer->conn, fwd);
            else
                encode(&peer->offline, fwd);     // 对方不在线，存起来，上线时补发
            cJSON_Delete(fwd);
            log_msg("%s -> %s", c->user->name, peer->name);
        }
        cJSON_AddStringToObject(reply, "type", "ack");
        cJSON_AddNumberToObject(reply, "ts", (double)ts);
    }
    send_json(c, reply);
    cJSON_Delete(reply);
}
```

- **先检查字段的类型**：消息来自网络，任何字段都可能缺失或类型不对（第四章《HTTP 请求与 JSON》第 4 节）。没登录、缺字段的消息直接忽略。
- **去重** `seen_before` 在用户最近 256 个编号中查找，找到了就不再转发；没找到就记下来。只记最近的 256 个，是因为重发只会发生在断线重连的短时间内，不需要永远记住。
- **无论是否重复，都回复 `ack`**：重复说明客户端没收到上一次的 `ack`，必须再回一次，否则客户端会一直显示「发送中」。
- cJSON 的数字是 `double`，`id->valuedouble` 正是第 2.2 节说要用 52 位编号的原因。

### 3.4 发送

```c
static void send_json(struct conn *c, cJSON *obj)
{
    encode(&c->out, obj);
    flush(c);                                    // 出错的连接会在下一次读的时候被关闭
}

// 尽量把 out 中的数据写出去；返回 -1 表示连接出错
static int flush(struct conn *c)
{
    while (c->out.len > 0) {
        ssize_t w = write(c->fd, c->out.data, c->out.len);
        if (w == -1) {
            if (errno == EAGAIN) break;          // 发送缓冲区满了，等 EPOLLOUT
            return -1;
        }
        buf_consume(&c->out, w);
    }
    watch(c);                                    // 还有没写完的，就关注 EPOLLOUT
    return 0;
}
```

和回声服务器一样：非阻塞的 `write` 可能只写出一部分，剩下的留在 `out` 中，关注 `EPOLLOUT`，等可写时再继续。回声服务器的 `pending` 是固定大小的，这里的 `out` 可以增长，因为服务器要转发别人的消息：一个客户端读得慢，不能让发给它的消息丢掉。

> 这个服务器没有限制 `out` 的大小。一个客户端如果只发不收（或者网络很慢），服务器为它缓存的数据会无限增长，这正是第三章《跨线程的信号与槽》第 4 节说的「队列堆积」。真实的服务器要给每个连接的发送缓冲区设上限，超过就断开它。这留作练习。

### 3.5 在 Docker 中运行

```dockerfile
# 在 Ubuntu 24.04 中编译、运行聊天服务器：
#   docker build -t chatd .
#   docker run --rm -p 9000:9000 --name chatd chatd
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends gcc libc6-dev libcjson-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY chatd.c .
RUN gcc -O2 -Wall -Wextra -o /usr/local/bin/chatd chatd.c -lcjson
EXPOSE 9000
CMD ["chatd", "9000"]
```

epoll 是 Linux 的接口，macOS 上没有（macOS 对应的是 kqueue），所以服务器在 Docker 的 Ubuntu 容器中运行，`-p 9000:9000` 把容器的 9000 端口映射到本机，Mac 上的客户端连接 `127.0.0.1:9000` 即可。用 `-Wall -Wextra` 编译，没有任何警告。

## 4. 用脚本测试协议

写客户端之前，先用一个 Python 脚本把协议的每一项都测一遍，这比通过界面一个个点要快得多，也更可靠：

```python
a = conn(); send(a, {"type": "login", "user": "lucky"}); print("lucky 登录:", recv_all(a))
b = conn(); send(b, {"type": "login", "user": "zhangsan"}); print("zhangsan 登录:", recv_all(b))
print("lucky 收到:", recv_all(a))
send(a, {"type": "msg", "id": 1, "to": "zhangsan", "text": "你好"})
print("lucky 收到:", recv_all(a)); print("zhangsan 收到:", recv_all(b))
send(a, {"type": "msg", "id": 1, "to": "zhangsan", "text": "你好"})      # 同一个编号再发一次
print("重发 id=1，lucky 收到:", recv_all(a)); print("zhangsan 收到:", recv_all(b))
b.close(); print("zhangsan 断开后 lucky 收到:", recv_all(a))
send(a, {"type": "msg", "id": 2, "to": "zhangsan", "text": "离线消息"}); print("lucky 收到:", recv_all(a))
b = conn(); send(b, {"type": "login", "user": "zhangsan"}); print("zhangsan 重新登录收到:", recv_all(b))
c = conn(); send(c, {"type": "login", "user": "zhangsan"}); print("zhangsan 在别处登录:", recv_all(c))
print("旧连接收到:", recv_all(b), "，旧连接 recv:", b.recv(10))
send(a, {"type": "msg", "id": 3, "to": "nobody", "text": "x"}); print("发给不存在的用户:", recv_all(a))
```

（`conn`、`send`、`recv_all` 是几行辅助函数：连接、按格式发送、读取一段时间内收到的所有消息并解析。）

```
lucky 登录: [{'type': 'login_ok', 'user': 'lucky', 'users': []}]
zhangsan 登录: [{'type': 'login_ok', 'user': 'zhangsan', 'users': [{'name': 'lucky', 'online': True}]}]
lucky 收到: [{'type': 'presence', 'user': 'zhangsan', 'online': True}]
lucky 收到: [{'id': 1, 'type': 'ack', 'ts': 1793445574705}]
zhangsan 收到: [{'type': 'msg', 'from': 'lucky', 'id': 1, 'text': '你好', 'ts': 1793445574705}]
重发 id=1，lucky 收到: [{'id': 1, 'type': 'ack', 'ts': 1793445577322}]
zhangsan 收到: []
zhangsan 断开后 lucky 收到: [{'type': 'presence', 'user': 'zhangsan', 'online': False}]
lucky 收到: [{'id': 2, 'type': 'ack', 'ts': 1793445581238}]
zhangsan 重新登录收到: [{'type': 'login_ok', ...}, {'type': 'msg', 'from': 'lucky', 'id': 2, 'text': '离线消息', 'ts': 1793445581238}]
zhangsan 在别处登录: [{'type': 'login_ok', 'user': 'zhangsan', 'users': [{'name': 'lucky', 'online': True}]}]
旧连接收到: [{'type': 'kicked'}] ，旧连接 recv: b''
发给不存在的用户: [..., {'id': 3, 'type': 'error', 'reason': '没有这个用户'}]
```

服务器的日志：

```
chatd 监听端口 9000
[70793] lucky 上线
[72098] zhangsan 上线
[74705] lucky -> zhangsan
[77322] lucky 重发的消息，已经处理过，只回复确认
[79932] zhangsan 下线
[81238] lucky -> zhangsan
[82544] zhangsan 上线
[83851] zhangsan 在别处登录，关闭旧的连接
[83852] zhangsan 上线
```

逐项对照第 2 节的设计：

- 登录后拿到其他用户的列表，其他人收到 `presence`；
- 编号 1 的消息转发给了 zhangsan；**同一个编号再发一次，lucky 又收到了 `ack`，zhangsan 什么也没收到**，日志中是「重发的消息，已经处理过」；
- zhangsan 断开，lucky 收到下线通知；这时发给 zhangsan 的消息，在他重新登录时紧跟着 `login_ok` 被补发；
- zhangsan 在另一个连接上登录，**旧连接先收到 `kicked`，然后 `recv` 返回空**（连接被关闭）；
- 发给不存在的用户，收到 `error`。

最初的版本中，被顶下线的旧连接是直接关闭的，没有 `kicked` 消息，同时还会向其他人再广播一次「zhangsan 上线」（他其实一直在线）。测试中发现了这两点，加上了 `kicked`，也只在用户从离线变为在线时才广播。

## 5. 下载

完整的服务器和客户端源码：[qt-chat.zip](/downloads/qt-chat.zip)。解压后：

```
README.md
server/            chatd.c、Dockerfile
client/            Qt 客户端（后面三篇逐一介绍）
```

```bash
cd server && docker build -t chatd . && docker run --rm -p 9000:9000 --name chatd chatd
```

## 6. 小结

- 协议用「长度 + JSON」，每条消息有 `type`：登录、在线状态、聊天、确认、错误、心跳、被顶下线。
- 可靠投递 = 客户端编号、先存后发、服务器确认、重连后重发未确认的、服务器按编号去重。编号用 52 位随机数，不同设备不会冲突，JSON 也能精确表示。
- 离线消息存在服务器，登录时补发。重复登录时先发 `kicked` 再关闭旧连接，客户端收到后停止自动重连，避免两边互相踢。
- 服务器在 epoll 回声服务器的基础上，增加了可增长的收发缓冲区、分帧、用户表。
- 写界面之前，先用脚本把协议测一遍。

## 练习

1. 给服务器的发送缓冲区加上限（例如 1 MB），超过就断开这个连接。用一个只登录、从不读取的 Python 客户端，让别人不断给它发消息来验证。
2. 服务器重启后，所有用户和离线消息都没了。用 SQLite（C 接口）把它们保存下来。
3. `seen_before` 用的是线性查找，每条消息都要比较 256 次。用户很多、消息很多时，怎样改进？
4. 给协议加一个「对方已读」的回执：客户端打开一个会话时，告诉服务器「我读到了第几条」，服务器转告对方。消息格式应该怎样设计？
