---
title: 故障检测：心跳与超时
date: 2026-11-14 11:00:00
updated: 2026-11-14 11:00:00
categories:
  - 分布式系统
  - 分布式基础
tags:
  - 分布式系统
  - 故障检测
  - 心跳
order: 2
description: 一个节点没有回应，是它挂了，还是网络慢、包丢了，或者它只是暂停了几秒？本文用 UDP 心跳写一个故障检测器实测：心跳间隔 100 毫秒、丢包 5% 时，超时 150 毫秒两分钟内误判 62 次，250 毫秒误判 2 次，500 毫秒 0 次，和「连续丢几个心跳」的概率计算吻合；进程被杀后 442 毫秒检测到；进程被暂停 2 秒，检测器同样判定它挂了，恢复后才发现是误判，暂停期间积压的心跳在恢复时一起得到了回应。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解为什么在异步网络中无法可靠地区分「慢」和「死」；会用心跳和超时实现故障检测器；会用概率估算误判的频率；理解检测速度和误判率之间的取舍；知道进程暂停（垃圾回收、虚拟机迁移）为什么特别危险。
>
> **前置知识**：本章第 1 篇；《计算机网络》专栏的《UDP》。
>
> **实验环境**：同第 1 篇。检测器 `hb` 运行在 c1 上，被检测的是 n1 上的 kvd，它在同一个端口的 UDP 上回显心跳包。

## 1. 慢和死分不开

上一篇看到，一个请求超时之后，客户端不知道它执行了没有。把问题换成「对方还活着吗」，也是一样：

- 对方崩溃了；
- 对方活着，但网络把心跳丢了，或者延迟了；
- 对方活着，但暂停了几秒（垃圾回收、虚拟机被迁移、内存换页），暂停结束后又一切正常。

在这三种情况下，我们看到的都只是「很久没有收到消息」。理论上已经证明，在消息延迟没有上限的**异步网络**中，**不可能完美地检测故障**：任何一个检测器，要么可能把活着的节点判成死了（误判），要么可能永远发现不了死掉的节点。实际系统只能选一个超时，在**检测速度**和**误判率**之间取舍。

## 2. 心跳检测器

`hb` 每隔一个固定的间隔向对方发一个 UDP 心跳包（内容是序号），对方原样发回。超过超时时间没有收到任何回应，就**怀疑**对方挂了；之后又收到了回应，说明刚才是误判：

```c
while (nowUs() < end) {
    int64_t now = nowUs();
    if (now >= nextSend) {                       /* 按间隔发心跳 */
        sendto(fd, b, n, 0, (struct sockaddr *)&peer, sizeof peer);
        nextSend += interval * 1000;
    }
    int64_t lp = atomic_load(&lastPong);         /* 接收线程更新最后一次回应的时间 */
    if (!suspected && now - lp > timeout * 1000) {
        suspected = 1;                           /* 怀疑：太久没有回应 */
        suspicions++;
    } else if (suspected && lp > suspectSince) {
        suspected = 0;                           /* 又收到了：刚才是误判 */
        falseAlarms++;
    }
    sleepMs(1);
}
```

用 UDP 而不用 TCP，是因为 TCP 会自己重传：一个心跳丢了，TCP 会在 200 毫秒后重发，把丢包变成延迟，检测器看到的就不是网络的真实情况了。

时间全部用 `CLOCK_MONOTONIC` 计算。墙上时钟（`CLOCK_REALTIME`）会被 NTP 调整，可能突然跳几秒，用它算超时，时钟一跳就会误判（第 5 篇会专门讨论时钟）。

<figure class="diagram">
<svg viewBox="0 0 700 210" role="img" aria-label="心跳检测：每 100 毫秒发一个心跳，对方原样回复。超过超时时间（这里 500 毫秒）一个回复都没有收到，就怀疑对方挂了；之后又收到回复，说明刚才是误判">
  <defs><marker id="hb-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="hb-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <line x1="60.0" y1="60.0" x2="665.0" y2="60.0" style="stroke:var(--border-2);stroke-width:1.2"/><text x="50.0" y="64.0" text-anchor="end" style="fill:var(--fg);font-size:12px">hb</text><line x1="60.0" y1="140.0" x2="665.0" y2="140.0" style="stroke:var(--border-2);stroke-width:1.2"/><text x="50.0" y="144.0" text-anchor="end" style="fill:var(--fg);font-size:12px">n1</text><line x1="60.0" y1="62" x2="74.0" y2="138" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#hb-a)"/><line x1="78.0" y1="138" x2="92.0" y2="62" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#hb-a)"/><line x1="115.0" y1="62" x2="129.0" y2="138" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#hb-a)"/><line x1="133.0" y1="138" x2="147.0" y2="62" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#hb-a)"/><line x1="170.0" y1="62" x2="184.0" y2="138" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#hb-a)"/><line x1="188.0" y1="138" x2="202.0" y2="62" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#hb-a)"/><line x1="225.0" y1="62" x2="239.0" y2="138" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#hb-a)"/><path d="M246.0,96 L254.0,104 M254.0,96 L246.0,104" style="stroke:var(--danger);stroke-width:2.2"/><line x1="280.0" y1="62" x2="294.0" y2="138" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#hb-a)"/><path d="M301.0,96 L309.0,104 M309.0,96 L301.0,104" style="stroke:var(--danger);stroke-width:2.2"/><line x1="335.0" y1="62" x2="349.0" y2="138" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#hb-a)"/><path d="M356.0,96 L364.0,104 M364.0,96 L356.0,104" style="stroke:var(--danger);stroke-width:2.2"/><line x1="390.0" y1="62" x2="404.0" y2="138" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#hb-a)"/><path d="M411.0,96 L419.0,104 M419.0,96 L411.0,104" style="stroke:var(--danger);stroke-width:2.2"/><line x1="445.00000000000006" y1="62" x2="459.00000000000006" y2="138" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#hb-a)"/><path d="M466.00000000000006,96 L474.00000000000006,104 M474.00000000000006,96 L466.00000000000006,104" style="stroke:var(--danger);stroke-width:2.2"/><line x1="500.00000000000006" y1="62" x2="514.0" y2="138" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#hb-a)"/><path d="M521.0,96 L529.0,104 M529.0,96 L521.0,104" style="stroke:var(--danger);stroke-width:2.2"/><line x1="555.0" y1="62" x2="569.0" y2="138" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#hb-a)"/><line x1="573.0" y1="138" x2="587.0" y2="62" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#hb-a)"/><line x1="610.0" y1="62" x2="624.0" y2="138" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#hb-a)"/><line x1="628.0" y1="138" x2="642.0" y2="62" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#hb-a)"/><rect x="202.0" y="30" width="275.0" height="18" rx="3" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="339.5" y="43.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">500 ms 没有回复</text><text x="481.0" y="43.0" text-anchor="start" style="fill:var(--danger);font-size:11px">→ 怀疑</text><text x="591.0" y="24.0" text-anchor="start" style="fill:var(--accent);font-size:11px">又收到 → 误判</text><text x="20.0" y="186.0" text-anchor="start" style="fill:var(--muted);font-size:11px">回复被丢（✕）：可能是网络丢包、对方崩溃，也可能是对方暂停了。检测器分不出来</text>
  </g>
</svg>
</figure>

## 3. 超时越短，误判越多

心跳间隔 100 毫秒，同时运行 4 个检测器，超时分别是 150、250、500、1000 毫秒，每个运行 120 秒。先只给 n1 加 1 毫秒延迟，不丢包：

```
timeout 150: sent 1200, replies 1200 (0.00% lost), suspicions 0, false alarms 0
timeout 250: sent 1200, replies 1200 (0.00% lost), suspicions 0, false alarms 0
...
rtt p50 1.325 p99 1.429 max 1.551 ms
```

网络正常时，150 毫秒的超时也绰绰有余。再给 n1 加上 5% 的丢包：

| 超时 | 两分钟内的误判 | 被误判为挂了的总时间 |
|---|---|---|
| 150 ms | **62 次** | 3522 ms |
| 250 ms | 2 次 | 101 ms |
| 500 ms | 0 | 0 |
| 1000 ms | 0 | 0 |

<figure class="diagram">
<svg viewBox="0 0 700 210" role="img" aria-label="心跳间隔 100 毫秒、丢包 5% 时，两分钟内的误判次数：超时 150 毫秒 62 次（约等于丢掉的心跳数），250 毫秒 2 次（要连续丢 2 个），500 和 1000 毫秒 0 次">
  <defs><marker id="hbfalse-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="hbfalse-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="130.0" y="36.0" text-anchor="end" style="fill:var(--fg);font-size:12px">超时 150 ms</text><rect x="140.0" y="20.0" width="272.8" height="22.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="420.8" y="36.0" text-anchor="start" style="fill:var(--fg);font-size:12px">62 次</text><text x="480.0" y="36.0" text-anchor="start" style="fill:var(--muted);font-size:11px">丢 1 个就误判，期望约 60</text><text x="130.0" y="76.0" text-anchor="end" style="fill:var(--fg);font-size:12px">超时 250 ms</text><rect x="140.0" y="60.0" width="8.8" height="22.0" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="156.8" y="76.0" text-anchor="start" style="fill:var(--fg);font-size:12px">2 次</text><text x="480.0" y="76.0" text-anchor="start" style="fill:var(--muted);font-size:11px">连丢 2 个，期望约 3</text><text x="130.0" y="116.0" text-anchor="end" style="fill:var(--fg);font-size:12px">超时 500 ms</text><rect x="140.0" y="100.0" width="1.5" height="22.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="148.0" y="116.0" text-anchor="start" style="fill:var(--fg);font-size:12px">0 次</text><text x="480.0" y="116.0" text-anchor="start" style="fill:var(--muted);font-size:11px">连丢 5 个，期望 0.0004</text><text x="130.0" y="156.0" text-anchor="end" style="fill:var(--fg);font-size:12px">超时 1000 ms</text><rect x="140.0" y="140.0" width="1.5" height="22.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="148.0" y="156.0" text-anchor="start" style="fill:var(--fg);font-size:12px">0 次</text><text x="480.0" y="156.0" text-anchor="start" style="fill:var(--muted);font-size:11px">连丢 10 个</text><text x="20.0" y="196.0" text-anchor="start" style="fill:var(--muted);font-size:11px">超时每多覆盖一个心跳间隔，误判的概率就乘以一次丢包率（5%）</text>
  </g>
</svg>
</figure>

这些数字可以算出来。心跳间隔 100 毫秒，要超过超时还没收到回应，需要**连续丢掉**若干个心跳：

- 超时 150 毫秒：丢 **1 个**心跳，两次回应就相隔 200 毫秒，超过了 150。1200 个心跳丢 5%，约 60 次，实测 62 次；
- 超时 250 毫秒：要连续丢 **2 个**（相隔 300 毫秒），概率 0.05² = 0.25%，1200 个心跳约 3 次，实测 2 次；
- 超时 500 毫秒：要连续丢 **5 个**，概率 0.05⁵ ≈ 3 × 10⁻⁷，两分钟内几乎不可能发生。

所以超时时间每多覆盖一个心跳间隔，误判的概率就乘以一次丢包率。代价是**检测得更慢**。

## 4. 检测要多久

超时 500 毫秒，在 n1 上用 `kill -9` 杀掉 kvd，同时记下时间：

```
KILL at 1793947428.783096849
1793947429.225 SUSPECT: no reply for 500 ms (last reply at 1793947428.724)
```

杀掉之后 **442 毫秒**检测到。最后一次回应是在杀掉之前 59 毫秒收到的，检测器从那时起算 500 毫秒。所以检测时间在「超时 − 间隔」到「超时」之间，这里是 400～500 毫秒。

想检测得更快，就要缩短超时，误判就更多；想少误判，就要加长超时，故障之后系统就要「瞎」更久。生产系统中常见的取值：Raft 的选举超时 150～300 毫秒（局域网），etcd 默认 1 秒，ZooKeeper 的会话超时几秒到几十秒，Kubernetes 判断一个节点失联则要几十秒。

## 5. 暂停：最危险的误判

还有一种情况比丢包更难处理：**进程暂停**。用 `SIGSTOP` 让 n1 的 kvd 停下 2 秒，再用 `SIGCONT` 恢复：

```
PAUSE at 1793947423.697797175
1793947424.124 SUSPECT: no reply for 500 ms (last reply at 1793947423.624)
RESUME at 1793947425.708200144
1793947425.709 ALIVE again: it was a false alarm, suspected for 1584 ms
```

<figure class="diagram">
<svg viewBox="0 0 700 220" role="img" aria-label="kvd 被 SIGSTOP 暂停 2 秒：427 毫秒后检测器判定它挂了；恢复后 1 毫秒回复又来了，误判持续 1584 毫秒；暂停期间的 20 个心跳排在接收缓冲区中，恢复时一起得到回复，往返时间最长 1985 毫秒">
  <defs><marker id="pause-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="pause-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <line x1="40.0" y1="70.0" x2="678.0" y2="70.0" style="stroke:var(--border-2);stroke-width:1.2"/><text x="40.0" y="60.0" text-anchor="start" style="fill:var(--muted);font-size:11px">hb 看到的</text><line x1="40.0" y1="150.0" x2="678.0" y2="150.0" style="stroke:var(--border-2);stroke-width:1.2"/><text x="40.0" y="172.0" text-anchor="start" style="fill:var(--muted);font-size:11px">n1 的 kvd</text><rect x="106.0" y="140" width="442.2" height="20" rx="3" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="327.1" y="155.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">暂停（SIGSTOP），2010 ms</text><rect x="199.9" y="60" width="348.5" height="20" rx="3" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="374.2" y="75.0" text-anchor="middle" style="fill:var(--danger);font-size:11px">判定为挂了：1584 ms</text><line x1="106.0" y1="90.0" x2="106.0" y2="140.0" style="stroke:var(--border-2);stroke-width:1.2;stroke-dasharray:4 4"/><text x="106.0" y="104.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">暂停</text><line x1="199.9" y1="82.0" x2="199.9" y2="130.0" style="stroke:var(--border-2);stroke-width:1.2;stroke-dasharray:4 4"/><text x="203.9" y="120.0" text-anchor="start" style="fill:var(--danger);font-size:11px">+427 ms 怀疑</text><line x1="548.2" y1="90.0" x2="548.2" y2="140.0" style="stroke:var(--border-2);stroke-width:1.2;stroke-dasharray:4 4"/><text x="544.2" y="104.0" text-anchor="end" style="fill:var(--muted);font-size:11px">恢复</text><line x1="548.6" y1="150" x2="554.6" y2="80" style="stroke:var(--accent);stroke-width:1"/><line x1="551.8" y1="150" x2="557.8" y2="80" style="stroke:var(--accent);stroke-width:1"/><line x1="555.0" y1="150" x2="561.0" y2="80" style="stroke:var(--accent);stroke-width:1"/><line x1="558.2" y1="150" x2="564.2" y2="80" style="stroke:var(--accent);stroke-width:1"/><line x1="561.4" y1="150" x2="567.4" y2="80" style="stroke:var(--accent);stroke-width:1"/><line x1="564.6" y1="150" x2="570.6" y2="80" style="stroke:var(--accent);stroke-width:1"/><line x1="567.8" y1="150" x2="573.8" y2="80" style="stroke:var(--accent);stroke-width:1"/><line x1="571.0" y1="150" x2="577.0" y2="80" style="stroke:var(--accent);stroke-width:1"/><line x1="574.2" y1="150" x2="580.2" y2="80" style="stroke:var(--accent);stroke-width:1"/><line x1="577.4" y1="150" x2="583.4" y2="80" style="stroke:var(--accent);stroke-width:1"/><line x1="580.6" y1="150" x2="586.6" y2="80" style="stroke:var(--accent);stroke-width:1"/><line x1="583.8" y1="150" x2="589.8" y2="80" style="stroke:var(--accent);stroke-width:1"/><line x1="587.0" y1="150" x2="593.0" y2="80" style="stroke:var(--accent);stroke-width:1"/><line x1="590.2" y1="150" x2="596.2" y2="80" style="stroke:var(--accent);stroke-width:1"/><line x1="593.4" y1="150" x2="599.4" y2="80" style="stroke:var(--accent);stroke-width:1"/><line x1="596.6" y1="150" x2="602.6" y2="80" style="stroke:var(--accent);stroke-width:1"/><line x1="599.8" y1="150" x2="605.8" y2="80" style="stroke:var(--accent);stroke-width:1"/><line x1="603.0" y1="150" x2="609.0" y2="80" style="stroke:var(--accent);stroke-width:1"/><line x1="606.2" y1="150" x2="612.2" y2="80" style="stroke:var(--accent);stroke-width:1"/><line x1="609.4" y1="150" x2="615.4" y2="80" style="stroke:var(--accent);stroke-width:1"/><text x="542.2" y="130.0" text-anchor="end" style="fill:var(--accent);font-size:11px">恢复后积压的 20 个心跳一起回复</text><text x="20.0" y="206.0" text-anchor="start" style="fill:var(--muted);font-size:11px">在检测器看来，暂停和崩溃一模一样；暂停的进程醒来后也不知道自己停过</text>
  </g>
</svg>
</figure>

暂停 427 毫秒后，检测器判定它挂了；恢复后 1 毫秒，回应又来了。从检测器的角度看，这和「崩溃了」没有任何区别，直到它恢复为止。

更有意思的是往返时间的统计：`rtt max 1985.494 ms`。暂停期间发来的 20 个心跳没有丢，而是排在 kvd 的 UDP 接收缓冲区中，恢复后被一口气处理完，每个都得到了回应。也就是说，**暂停的进程醒来后，会处理一批「过期」的消息，而它自己完全不知道刚才停了 2 秒**。

在真实系统中，这类暂停很常见：Java 的垃圾回收可能停顿几秒，虚拟机热迁移会冻结整个机器，内存不足时换页会让进程卡住。危险在于：如果被暂停的是**主节点**，其他节点检测到它「挂了」，选出了新的主节点；它醒来之后，**仍然以为自己是主节点**，继续处理写请求。这就是第 4 篇要测的**脑裂**。

## 6. 更好的检测器

固定超时的心跳很简单，实际系统有一些改进：

- **自适应超时**：根据最近的心跳到达间隔估计分布，算出「这么久还没来」的可疑程度（φ 累积故障检测器，Cassandra 和 Akka 使用），网络变差时自动放宽；
- **间接探测**：自己联系不上，就请另外几个节点帮忙探测，避免把「我和它之间的链路坏了」当成「它挂了」（SWIM 协议，Consul 使用）；
- **租约**：与其判断别人是否挂了，不如让每个节点的权力**自动过期**。主节点持有一个 10 秒的租约，必须在过期前续约，否则自己主动放弃主节点的身份；其他节点等租约过期后再接管。这依赖于各节点的时钟走得差不多快，第 5 篇会讨论。

无论哪种方法，都不能消除误判，只能降低它的概率。所以**系统必须在误判发生时也是正确的**，不能依赖「检测器说它挂了，它就一定挂了」。

## 7. 常见错误

1. **用 TCP 连接是否断开来判断故障**。对方崩溃时，如果没有包发过去，TCP 连接可能几十分钟都不会发现（除非开了 keepalive）；对方暂停时连接一直是好的。
2. **超时取得太短**。5% 丢包时，150 毫秒的超时两分钟误判 62 次。
3. **用墙上时钟计算超时**。NTP 一调整，可能一下子「超时」或永远不超时。
4. **以为检测器判定挂了，对方就真的停止了工作**。暂停的进程醒来后会继续工作。
5. **只从一个点检测**。可能是检测器自己的网络有问题。

## 8. 小结

- 在异步网络中，「慢」和「死」无法区分，故障检测器只能在检测速度和误判率之间取舍；
- 心跳间隔 100 毫秒、丢包 5%：超时 150 毫秒两分钟误判 62 次，250 毫秒 2 次，500 毫秒 0 次；每多覆盖一个间隔，误判概率乘以一次丢包率；
- 进程被杀后 442 毫秒检测到，检测时间在「超时 − 间隔」到「超时」之间；
- 暂停 2 秒的进程被判定为挂了，恢复后又开始回应，还处理了暂停期间积压的心跳；
- 系统必须在误判发生时也正确，这是后面几篇的主线。

## 9. 练习

1. 心跳间隔 100 毫秒，丢包率 1%，超时 350 毫秒。一天之内大约会误判几次？
2. 心跳间隔从 100 毫秒缩短到 10 毫秒，超时保持 500 毫秒。误判率和检测时间会怎样变化？代价是什么？
3. 节点 A 认为 B 挂了，而 C 认为 B 活着。这种情况可能发生吗？系统应该听谁的？

<details>
<summary>点击查看答案</summary>

1. 超时 350 毫秒，要连续丢 4 个心跳（两次回应相隔 500 毫秒）才会误判，概率 0.01⁴ = 10⁻⁸。一天发 86400 / 0.1 = 864000 个心跳，期望误判约 0.009 次，也就是大约一百多天一次。注意这假设丢包是独立的；实际的丢包常常是成片的（交换机拥塞、链路抖动），连续丢包的概率要大得多。

2. 超时 500 毫秒覆盖 50 个间隔，要连续丢 50 个才误判，独立丢包下几乎不可能；检测时间在 490～500 毫秒之间，比原来的 400～500 毫秒更稳定。代价是心跳包多了 10 倍：一个检测 1000 个节点的系统，每秒要收发 10 万个包。而且成片的丢包（比如网络中断 600 毫秒）无论心跳多密都会误判。

3. 可能：A 和 B 之间的链路断了，而 C 和 B 之间是好的（部分网络分区，第 4 篇的实验正是这样）。系统不应该听任何一个节点「单方面」的判断，而应该让多数节点达成一致，比如 Raft 中一个候选人要得到多数节点的投票才能成为新的主节点，少数派的看法不会造成影响。

</details>
