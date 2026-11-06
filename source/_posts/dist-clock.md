---
title: 时钟与事件顺序
date: 2026-11-14 14:00:00
updated: 2026-11-14 14:00:00
categories:
  - 分布式系统
  - 分布式基础
tags:
  - 分布式系统
  - 时钟
  - Lamport 时钟
order: 5
description: 用时间戳决定哪个写入更新，是最直观的做法，也是最容易出错的做法。本文先实测时钟有多不准：同一台 Mac 对两个 NTP 服务器测出的偏差是 +0.4 和 −6.7 毫秒，而测量本身的误差就有 ±83 和 ±40 毫秒；然后用「最后写入者胜」做实验：两个客户端先后写同一个键，后写的那个时钟慢 1 毫秒，它确认过的写入就有 4.1% 被丢掉，慢 10 毫秒丢 43%，慢 50 毫秒全部丢掉；改用 Lamport 逻辑时钟，同样慢 50 毫秒，一次也不丢。
top_group_list: true
disableNunjucks: true
viz: [dist-basics]
---

> **本文目标**：理解为什么不同机器的时钟无法精确同步，以及测量时钟偏差本身的不确定性；区分墙上时钟和单调时钟的用途；理解「最后写入者胜」在时钟偏差下为什么会丢掉确认过的写入；会用 Lamport 逻辑时钟给事件排序，知道它能保证什么、不能保证什么。
>
> **前置知识**：本章第 1、2 篇。
>
> **实验环境**：同第 1 篇。时钟偏差的测量在宿主机（macOS）上进行。

## 1. 时钟有多不准

每台机器的时钟由一个石英晶振驱动，它的频率会随温度变化，每天可能走快或走慢几秒。机器用 NTP 定期和时间服务器对时，把偏差控制在毫秒级。

但「和服务器差多少」本身就测不准。macOS 自带的 `sntp` 会报告偏差和误差范围：

```
$ sntp time.apple.com
+0.000381 +/- 0.083150 time.apple.com 2403:300:a16:4000::21
$ sntp ntp.aliyun.com
-0.006663 +/- 0.039663 ntp.aliyun.com 203.107.6.88
```

同一台机器，同一时刻，对一个服务器测出来快 0.4 毫秒，对另一个慢 6.7 毫秒，而**误差范围有 ±83 和 ±40 毫秒**。原因是：客户端发出请求，服务器在中间某个时刻读了自己的时钟，回复要经过网络才到达。客户端只知道往返用了多久，不知道去程和回程各占多少，只能假设各占一半，误差就是往返时间的一半。

<figure class="diagram">
<svg viewBox="0 0 700 210" role="img" aria-label="测量时钟偏差：客户端在 t0 发出请求，服务器在某个时刻读自己的时钟得到 T，客户端在 t1 收到回复。客户端只能假设 T 对应 (t0 + t1) / 2，真实时刻可能在 t0 到 t1 之间任何位置，误差是往返时间的一半">
  <defs><marker id="ntp-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="ntp-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <line x1="60.0" y1="60.0" x2="650.0" y2="60.0" style="stroke:var(--border-2);stroke-width:1.2"/><text x="50.0" y="64.0" text-anchor="end" style="fill:var(--fg);font-size:12px">客户端</text><line x1="60.0" y1="150.0" x2="650.0" y2="150.0" style="stroke:var(--border-2);stroke-width:1.2"/><text x="50.0" y="154.0" text-anchor="end" style="fill:var(--fg);font-size:12px">服务器</text><line x1="120" y1="62" x2="330" y2="148" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#ntp-a)"/><line x1="370" y1="148" x2="600" y2="62" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#ntp-a)"/><circle cx="120.0" cy="60.0" r="4" style="fill:var(--accent)"/><circle cx="600.0" cy="60.0" r="4" style="fill:var(--accent)"/><text x="120.0" y="46.0" text-anchor="middle" style="fill:var(--fg);font-size:12px">t0</text><text x="600.0" y="46.0" text-anchor="middle" style="fill:var(--fg);font-size:12px">t1</text><circle cx="350.0" cy="150.0" r="5" style="fill:var(--warn)"/><text x="350.0" y="174.0" text-anchor="middle" style="fill:var(--fg);font-size:12px">读时钟：T</text><rect x="120" y="18" width="480" height="12" rx="3" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="360.0" y="12.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">T 可能对应其中任何时刻：误差 ±(t1 − t0) / 2</text><text x="20.0" y="200.0" text-anchor="start" style="fill:var(--muted);font-size:11px">去程和回程的时间未必相等，客户端无从得知</text>
  </g>
</svg>
</figure>

再测一下 Docker 虚拟机和 Mac 的时钟：在 Mac 上记下时间，在虚拟机中读时间，再在 Mac 上记一次时间，取两次的中点比较，测 5 次：

```
VM - Mac = +12.9 ms  (±29.9 ms)
VM - Mac = -1.4 ms  (±16.4 ms)
VM - Mac = +0.4 ms  (±16.6 ms)
VM - Mac = -3.0 ms  (±13.5 ms)
VM - Mac = -3.1 ms  (±13.3 ms)
```

`docker exec` 本身要十几到几十毫秒，所以误差范围很大，只能说两者相差在几毫秒以内。本文实验中的 4 个容器运行在同一个内核上，**共用一个时钟**，它们之间没有偏差，所以下面的实验中，时钟偏差是程序**人为加上的**。真实的多台机器之间，NTP 同步良好时偏差通常在 1 毫秒到几十毫秒之间；NTP 配置出错、虚拟机暂停后恢复时，偏差可以达到几秒甚至更多。

## 2. 两种时钟

Linux 上有两种常用的时钟：

| | `CLOCK_REALTIME`（墙上时钟） | `CLOCK_MONOTONIC`（单调时钟） |
|---|---|---|
| 含义 | 现在是几点 | 从开机起过了多久 |
| 会被 NTP 调整吗 | 会，可能突然往前或往后跳 | 只会微调快慢，不会跳 |
| 不同机器之间能比较吗 | 大致可以（差几毫秒） | 不能，各自从开机算起 |
| 用途 | 记录事件发生的时刻、日志 | 计算超时、测量耗时 |

dkv 中所有的超时都用 `nowUs()`（单调时钟），只有打印日志用 `wallUs()`（墙上时钟）。用墙上时钟计算超时，NTP 一往回调，超时就可能永远不触发。

问题出在下一种用法上：**用墙上时钟的时间戳，比较不同机器上事件的先后**。

## 3. 最后写入者胜

多个副本都能接受写入时（第 6 篇的无主复制、多数据中心复制），同一个键可能被两个客户端同时修改，副本之间要决定保留哪一个。最简单的规则是**最后写入者胜**（Last Write Wins，LWW）：每次写入带上写入时刻的时间戳，副本保留时间戳最大的那个。Cassandra 默认就是这样做的。

kvd 的 `VPUT k v ver` 实现了这个规则：只有版本号（这里是时间戳）更大才覆盖。

```c
} else if (sscanf(line, "VPUT %31s %63s %lu", k, v, &a) == 3) {
    Slot *s = lookup(k, 1);
    if (a > s->ver || (a == s->ver && strcmp(v, s->val) > 0)) {   /* 更新才覆盖 */
        s->ver = a;
        snprintf(s->val, sizeof s->val, "%s", v);
    }
    snprintf(out, sizeof out, "OK %lu\n", a);                     /* 无论覆盖没有，都回复 OK */
}
```

实验 `cli lww`：两个客户端 A 和 B 轮流写同一个键，写入发给 n1、n2、n3 三个副本，等全部确认：

1. A 写入 `a1`，时间戳取自 A 的时钟，三个副本都确认；
2. A 告诉 B「我写完了」，B 等待 0～20 毫秒（随机）后写入 `b1`，时间戳取自 B 的时钟，三个副本都确认；
3. 读回来。B 的写入在 A 之后，并且**是在知道 A 写完之后才写的**，结果应该是 `b1`。

重复 1000 轮。B 的时钟比 A 慢若干毫秒（程序中给 B 的时间戳减去一个偏差）：

```
B's clock behind by 0.0 ms:  B's acked write lost 0 times (0.0%)
B's clock behind by 1.0 ms:  B's acked write lost 41 times (4.1%)
B's clock behind by 5.0 ms:  B's acked write lost 200 times (20.0%)
B's clock behind by 10.0 ms: B's acked write lost 430 times (43.0%)
B's clock behind by 50.0 ms: B's acked write lost 1000 times (100.0%)
```

<figure class="diagram">
<svg viewBox="0 0 700 220" role="img" aria-label="最后写入者胜：B 在 A 写完后 0～20 毫秒写入，B 的时钟慢若干毫秒时，B 确认过的写入被丢掉的比例。实测 0 毫秒 0%，1 毫秒 4.1%，5 毫秒 20.0%，10 毫秒 43.0%，50 毫秒 100%；Lamport 时钟在慢 50 毫秒时 0%">
  <defs><marker id="lww-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="lww-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="160.0" y="27.0" text-anchor="end" style="fill:var(--fg);font-size:12px">B 慢 0 ms</text><text x="178.0" y="27.0" text-anchor="start" style="fill:var(--fg);font-size:11px">0</text><text x="160.0" y="57.0" text-anchor="end" style="fill:var(--fg);font-size:12px">B 慢 1 ms</text><rect x="170.0" y="42.0" width="14.8" height="20.0" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="192.8" y="57.0" text-anchor="start" style="fill:var(--fg);font-size:11px">4.1%</text><line x1="187.3" y1="40" x2="187.3" y2="64" style="stroke:var(--fg-2);stroke-width:1.5;stroke-dasharray:3 2"/><text x="160.0" y="87.0" text-anchor="end" style="fill:var(--fg);font-size:12px">B 慢 5 ms</text><rect x="170.0" y="72.0" width="72.0" height="20.0" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="250.0" y="87.0" text-anchor="start" style="fill:var(--fg);font-size:11px">20.0%</text><line x1="255.7" y1="70" x2="255.7" y2="94" style="stroke:var(--fg-2);stroke-width:1.5;stroke-dasharray:3 2"/><text x="160.0" y="117.0" text-anchor="end" style="fill:var(--fg);font-size:12px">B 慢 10 ms</text><rect x="170.0" y="102.0" width="154.8" height="20.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="332.8" y="117.0" text-anchor="start" style="fill:var(--fg);font-size:11px">43.0%</text><line x1="341.4" y1="100" x2="341.4" y2="124" style="stroke:var(--fg-2);stroke-width:1.5;stroke-dasharray:3 2"/><text x="160.0" y="147.0" text-anchor="end" style="fill:var(--fg);font-size:12px">B 慢 50 ms</text><rect x="170.0" y="132.0" width="360.0" height="20.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="538.0" y="147.0" text-anchor="start" style="fill:var(--fg);font-size:11px">100.0%</text><line x1="530.0" y1="130" x2="530.0" y2="154" style="stroke:var(--fg-2);stroke-width:1.5;stroke-dasharray:3 2"/><text x="160.0" y="177.0" text-anchor="end" style="fill:var(--fg);font-size:12px">Lamport，慢 50 ms</text><text x="178.0" y="177.0" text-anchor="start" style="fill:var(--accent);font-size:11px">0（一次不丢）</text><text x="20.0" y="206.0" text-anchor="start" style="fill:var(--muted);font-size:11px">虚线：估算值 偏差 / 21 ms（B 的等待时间均匀取 0～20 ms）</text>
  </g>
</svg>
</figure>

B 的时钟只慢 1 毫秒，**它确认过的写入就有 4.1% 消失了**：三个副本都回复了 OK，但都没有真正覆盖，因为 B 的时间戳比 A 的还小。慢 50 毫秒时，B 的写入**全部**被丢掉，而 B 收到的每一个回复都是 OK。

<figure class="diagram">
<svg viewBox="0 0 700 210" role="img" aria-label="为什么 B 的写入被丢掉：A 在真实时间 0 写入，时间戳取自 A 的时钟为 1000；B 在真实时间 3 毫秒（A 写完之后）写入，但 B 的时钟慢 10 毫秒，时间戳为 993；副本比较时间戳，认为 B 的写入更旧，没有覆盖，却照样回复 OK">
  <defs><marker id="lwwtime-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="lwwtime-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <line x1="80.0" y1="50.0" x2="620.0" y2="50.0" style="stroke:var(--border-2);stroke-width:1.2"/><text x="70.0" y="54.0" text-anchor="end" style="fill:var(--fg);font-size:12px;font-weight:700">A</text><line x1="80.0" y1="120.0" x2="620.0" y2="120.0" style="stroke:var(--border-2);stroke-width:1.2"/><text x="70.0" y="124.0" text-anchor="end" style="fill:var(--fg);font-size:12px;font-weight:700">B</text><line x1="80.0" y1="180.0" x2="620.0" y2="180.0" style="stroke:var(--border-2);stroke-width:1.2;stroke-dasharray:4 4"/><text x="70.0" y="184.0" text-anchor="end" style="fill:var(--muted);font-size:11px">真实</text><text x="80.0" y="198.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">-2 ms</text><text x="170.0" y="198.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">0 ms</text><text x="260.0" y="198.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">2 ms</text><text x="350.0" y="198.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">4 ms</text><text x="440.0" y="198.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">6 ms</text><text x="530.0" y="198.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">8 ms</text><text x="620.0" y="198.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">10 ms</text><circle cx="170.0" cy="50.0" r="5" style="fill:var(--accent)"/><rect x="100" y="14" width="140" height="26" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="170.0" y="31.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">写 a，时间戳 1000</text><path d="M170.0,54 C215.0,90 260.0,90 301.0,116" style="fill:none;stroke:var(--fg-2);stroke-width:1.5;stroke-dasharray:5 4" marker-end="url(#lwwtime-a)"/><text x="242.0" y="86.0" text-anchor="start" style="fill:var(--muted);font-size:11px">「我写完了」</text><circle cx="305.0" cy="120.0" r="5" style="fill:var(--danger)"/><rect x="317" y="132" width="250" height="26" rx="6" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="442.0" y="149.2" text-anchor="middle" style="fill:var(--fg);font-size:11px">写 b，时间戳 993（时钟慢 10 ms）</text><text x="449.0" y="60.0" text-anchor="start" style="fill:var(--danger);font-size:11px">副本：993 &lt; 1000，保留 a，回复 OK</text>
  </g>
</svg>
</figure>

丢失的比例可以估算：B 在 A 写完后等 0～20 毫秒（21 种取值），如果等待时间小于时钟偏差，B 的时间戳就比 A 的小。偏差 1 毫秒时只有等 0 毫秒的那 1/21 ≈ 4.8% 会丢，偏差 5 毫秒是 5/21 ≈ 24%，10 毫秒是 10/21 ≈ 48%，和实测接近（实际的等待会比设定的稍长一点，所以实测略低）。

这就是 LWW 的问题：**它丢弃写入时，不会告诉任何人**。在一个时钟慢的节点上的写入，在其他节点看来总是「旧的」。

## 4. Lamport 逻辑时钟

仔细看，实验中真正需要的不是「几点几分」，而是**先后关系**：B 的写入是在 A 写完之后才发生的，它就应该排在 A 后面。Lamport 在 1978 年提出了**逻辑时钟**，专门刻画这种关系：

1. 每个进程维护一个计数器 L，发生一个事件（比如一次写入）就加一，用加一后的值作为这个事件的时间戳；
2. 发送消息时带上自己的 L；
3. 收到消息时，把 L 设为 max(L, 消息中的 L)，再加一。

如果事件 x 能通过消息链影响到事件 y（x **先于** y 发生，记作 x → y），那么一定有 L(x) < L(y)。时间戳相同时用进程编号打破平局，就得到所有事件的一个全序。

<div class="dsviz" data-viz="dist-lamport"></div>

`cli lww` 的 `lamport` 模式：A 和 B 各有一个计数器，A 写完后通知 B 时带上自己的计数，B 取较大值再加一：

```c
uint64_t tsA = (++lA) << 8 | 1;                    /* 低 8 位是进程编号，打破平局 */
... A 写入 ...
if ((tsA >> 8) > lB) lB = tsA >> 8;                /* B 收到 A 的通知 */
uint64_t tsB = (++lB) << 8 | 2;                    /* B 的写入一定大于 A 的 */
```

同样让 B 的物理时钟慢 50 毫秒（逻辑时钟完全不用物理时钟，这个偏差对它没有影响）：

```
rounds 1000, B's clock behind by 50.0 ms, mode lamport: B's acked write lost 0 times (0.0%)
```

**一次也没有丢**。

## 5. 逻辑时钟不能做什么

Lamport 时钟保证「x 先于 y，则 L(x) < L(y)」，**反过来不成立**：L(x) < L(y) 不代表 x 先于 y，它们可能是**并发**的，互相不知道对方。

- 两个客户端互不通信、同时修改一个键，Lamport 时钟会给出一个顺序，保留一个、丢弃另一个，但这个顺序是任意的，被丢弃的写入同样不会通知任何人。要**检测**并发（而不是随便丢一个），需要**向量时钟**：每个节点记一个计数器数组，可以判断两个事件是先后还是并发，并发时把两个值都留下，交给应用合并（Dynamo、Riak 的做法）；
- 逻辑时钟和真实时间无关。「查询 5 分钟之前的数据」「这个租约 10 秒后过期」这类需求，还是离不开物理时钟。

实际系统中的折中：

- **混合逻辑时钟**（HLC，CockroachDB 使用）：时间戳的高位是物理时间，低位是逻辑计数器，既接近真实时间，又保证因果关系；
- **TrueTime**（Google Spanner）：用 GPS 和原子钟把误差范围控制在几毫秒以内，并且**明确给出误差范围** [最早, 最晚]；提交事务时故意等待误差范围的时间，保证提交的先后和真实时间一致；
- **不依赖时钟**：用一个主节点（或者 Raft 选出的领导者）给所有写入排序，日志的编号就是顺序。这是下一章的路线。

## 6. 常见错误

1. **用墙上时钟计算超时或耗时**。NTP 调整时会出错，应该用单调时钟。
2. **用不同机器的时间戳比较先后**。时钟慢 1 毫秒，LWW 就丢了 4.1% 的写入。
3. **以为 NTP 同步之后时钟就一致了**。偏差测量本身就有几十毫秒的误差范围。
4. **以为 LWW 丢数据时会报错**。被丢的写入同样收到了 OK。
5. **以为 Lamport 时钟能判断并发**。它只给出一个和因果一致的顺序，并发的写入照样会被任意丢弃一个。

## 7. 小结

- 时钟偏差测不准：对两个 NTP 服务器测出 +0.4 和 −6.7 毫秒，误差范围 ±83 和 ±40 毫秒，因为不知道网络往返中去程和回程各占多少；
- 墙上时钟用来记时刻，单调时钟用来算超时；
- 最后写入者胜 + 时钟偏差：后写的客户端时钟慢 1 毫秒，4.1% 确认过的写入消失；慢 10 毫秒 43%，慢 50 毫秒 100%；
- Lamport 时钟沿着消息传递因果关系，同样的实验一次也不丢；
- 它不能识别并发，也和真实时间无关；需要识别并发用向量时钟，需要真实时间用 HLC 或 TrueTime，或者干脆由一个领导者排序。

## 8. 练习

1. 进程 P1 的事件：a（L=1），发消息给 P2（L=2）。P2 的事件：b（L=1），收到 P1 的消息（L=?），c（L=?）。填出问号，并判断 a 和 b 谁先发生。
2. 一个系统用 LWW，时钟偏差最大 10 毫秒。如果要求「同一个键的两次写入之间至少隔 1 秒」，还会丢写入吗？
3. Spanner 提交事务前要等待「误差范围」那么久。如果误差范围是 7 毫秒，每秒最多能提交多少个有冲突的事务？

<details>
<summary>点击查看答案</summary>

1. P2 收到消息时 L = max(1, 2) + 1 = 3，c 是 4。a 和 b 是并发的：它们之间没有消息链，谁也影响不到谁。虽然 L(a) = L(b) = 1，按进程编号会把 a 排在 b 前面，但这只是一个任意的约定，不代表真实的先后。

2. 如果两次写入确实隔了 1 秒（以真实时间计），而偏差最多 10 毫秒，后一次的时间戳至少比前一次大 1000 − 10 = 990 毫秒，不会丢。但「隔 1 秒」本身要由谁来保证？如果两次写入来自两个互不通信的客户端，没人能保证这一点；而且「偏差最多 10 毫秒」也只是假设，NTP 出故障时它就不成立了。

3. 有冲突的事务必须一个接一个地提交，每个都要等 7 毫秒，所以每秒最多约 1000 / 7 ≈ 140 个（不算其他开销）。没有冲突的事务可以同时等，不受这个限制。这就是 Google 为什么要花大力气把误差范围降到个位数毫秒：误差范围直接决定了冲突事务的吞吐量。

</details>
