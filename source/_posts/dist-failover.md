---
title: 故障转移与脑裂
date: 2026-11-14 13:00:00
updated: 2026-11-14 13:00:00
categories:
  - 分布式系统
  - 分布式基础
tags:
  - 分布式系统
  - 复制
  - 脑裂
order: 4
description: 主节点挂了，从节点自动接管，听起来很简单。本文给 dkv 加上自动故障转移：从节点 1 秒收不到主节点的心跳就自己当主节点。然后只切断旧主节点和这个从节点之间的网络，旧主节点其实还活着。实测：异步复制时两个主节点同时接受写入 2.9 秒，旧主节点确认的 3881 条写入在新主节点上不存在，两个从节点的数据也分叉了；等多数确认也挡不住，丢了 3009 条；加上任期号后旧主节点 2 毫秒内就退位了，但分区到晋升之间那 1 秒确认的 1000 条写入还是丢了，因为晋升的不是最新的节点；两个从节点同时晋升时，两个主节点的任期号都是 2，任期号也分不出谁对。这些问题需要投票来解决，这就是下一章的 Raft。
top_group_list: true
disableNunjucks: true
viz: [dist-basics]
---

> **本文目标**：理解自动故障转移的基本做法和它的风险；理解什么是脑裂，会用 `iptables` 制造部分网络分区；理解任期号（epoch、fencing token）如何阻止旧主节点继续写入，以及它不能解决什么；理解为什么故障转移需要多数投票，并且只能选出日志最新的节点。
>
> **前置知识**：本章第 2、3 篇。
>
> **实验环境**：同第 1 篇。n1 是主节点，n2、n3 是从节点，等 1 个从节点确认时即为多数。客户端在 c1 上，每个客户端每秒写 1000 次。

## 1. 自动故障转移

第 3 篇中，主节点挂了之后需要人手动提升一个从节点。自动化的想法很直接：从节点用第 2 篇的心跳盯着主节点，太久没有回应，就自己当主节点。kvd 的 `-auto 1000` 就是这样做的：

```c
if (nowUs() - lastPong > autoMs * 1000) {          /* 主节点 1 秒没有回应 */
    pthread_mutex_lock(&mu);
    if (role == BACKUP) {
        epoch++;                                   /* 任期 +1，下面会用到 */
        role = PRIMARY;
        logmsg("primary silent for %d ms: promote myself, epoch %lu", autoMs, epoch);
    }
    pthread_mutex_unlock(&mu);
}
```

晋升之后，它的复制线程开始工作，把自己的日志发给其他节点。

但第 2 篇说过：「1 秒没有回应」不等于「挂了」。如果主节点**其实还活着**，只是和这个从节点之间的网络断了呢？

## 2. 实验：部分网络分区

只让 n2 盯着 n1。写入开始 2 秒后，在 n1 上用 `iptables` 切断它和 n2 之间的网络，**n1 和 n3、n1 和客户端之间仍然是通的**：

```bash
iptables -A INPUT -s 10.77.0.12 -j DROP
iptables -A OUTPUT -d 10.77.0.12 -j DROP
```

两个客户端同时写：客户端 a 一直写 n1，客户端 b 一直写 n2（n2 晋升之前会收到 `ERR notprimary`，晋升之后就能写了）。各写 6 秒后，用 `cli check` 核对每个客户端**收到 OK 的写入**在各节点上还在不在，用 `cli diff` 比较节点之间的数据。

<figure class="diagram">
<svg viewBox="0 0 700 230" role="img" aria-label="实验的网络：只切断 n1 和 n2 之间的网络（双向），n1 和 n3、n2 和 n3、客户端和所有节点之间都是通的。客户端 a 写 n1，客户端 b 写 n2；n2 盯着 n1 的心跳，1 秒收不到就自己晋升">
  <defs><marker id="topo-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="topo-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <rect x="40" y="130" width="150" height="60" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="115.0" y="156.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">n1：主节点</text><text x="115.0" y="172.1" text-anchor="middle" style="fill:var(--muted);font-size:11px">任期 1</text><rect x="510" y="130" width="150" height="60" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="585.0" y="156.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">n2：从节点</text><text x="585.0" y="172.1" text-anchor="middle" style="fill:var(--muted);font-size:11px">-auto 1000</text><rect x="275" y="30" width="150" height="56" rx="6" style="fill:var(--panel-2);stroke:var(--border-2);stroke-width:1.5"/><text x="350.0" y="62.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">n3：从节点</text><rect x="40" y="20" width="150" height="40" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="115.0" y="44.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">客户端 a</text><rect x="510" y="20" width="150" height="40" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="585.0" y="44.6" text-anchor="middle" style="fill:var(--fg);font-size:12px">客户端 b</text><line x1="190" y1="160" x2="510" y2="160" style="stroke:var(--danger);stroke-width:2;stroke-dasharray:6 4"/><path d="M341,151 L359,169 M359,151 L341,169" style="stroke:var(--danger);stroke-width:2.2"/><text x="350.0" y="190.0" text-anchor="middle" style="fill:var(--danger);font-size:11px">iptables DROP</text><line x1="190" y1="140" x2="275" y2="72" style="stroke:var(--fg-2);stroke-width:1.5"/><line x1="510" y1="140" x2="425" y2="72" style="stroke:var(--fg-2);stroke-width:1.5"/><line x1="115" y1="60" x2="115" y2="128" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#topo-a)"/><line x1="585" y1="60" x2="585" y2="128" style="stroke:var(--fg-2);stroke-width:1.5" marker-end="url(#topo-a)"/><text x="20.0" y="220.0" text-anchor="start" style="fill:var(--muted);font-size:11px">n1 还活着、还能服务客户端 a，只是 n2 联系不上它</text>
  </g>
</svg>
</figure>

这种「一部分节点之间断了、另一部分没断」的分区在真实网络中很常见：一个交换机端口坏了，一条防火墙规则配错了，一个机房之间的专线断了而公网还通着。

下面的演示把实验的过程一步步画出来，可以切换是否使用任期号：

<div class="dsviz" data-viz="dist-split"></div>

## 3. 异步复制：两个主节点

先用异步复制（`-acks 0`），不检查任期（`-fence 0`）：

```
partition n1<->n2 at 1793947603.982
1793947604.980 n2 primary silent for 1000 ms: promote myself, epoch 2
id=n1 role=primary epoch=1 log=6000
id=n2 role=primary epoch=2 log=7821
id=n3 role=backup epoch=2 log=7821
client a -> n1: acked 6000, errors 0
client b -> n2: acked 5702, errors 298
acked 6000, missing on 10.77.0.12:7000: 3881 (acked between 1793947603.983 and 1793947607.864)
acked 5702, missing on 10.77.0.13:7000: 1017 (acked between 1793947605.034 and 1793947606.087)
10.77.0.12:7000 keys 7821, 10.77.0.13:7000 keys 10685, only in first 1017, only in second 3881
```

分区 1.0 秒后 n2 晋升。从那以后，**n1 和 n2 都是主节点**，都在接受写入、都在告诉客户端「成功」，一直持续到实验结束，共 2.9 秒。这就是**脑裂**（split brain）。

- n1 一直不知道发生了什么，客户端 a 的 6000 次写入全部成功；其中分区之后的 **3881 条在新主节点 n2 上不存在**。如果之后以 n2 为准，它们就全丢了；
- n3 同时收到两个主节点的日志。两个主节点的日志编号是各自递增的，n2 连接 n3 时，n3 的日志比 n2 长（含有 n1 的写入），n2 就从 n3 的长度之后开始发，**中间那段 1017 条写入从来没有发给 n3**；
- 最后，n2 有 1017 个 n3 没有的键，n3 有 3881 个 n2 没有的键。**三个节点的数据都不一样**，没有一个是对的。

## 4. 等多数确认也挡不住

第 3 篇中，等 1 个从节点确认（多数）可以保证主节点崩溃时不丢数据。但在分区时：

```
== acks 1, fence 0
1793947623.832 n2 primary silent for 1000 ms: promote myself, epoch 2
id=n1 role=primary epoch=1 log=5134
client a -> n1: acked 5133, errors 1, first error at 1793947627.844, last ok at 1793947625.843
acked 5133, missing on 10.77.0.12:7000: 3009 (acked between 1793947622.835 and 1793947625.843)
```

n1 联系不上 n2，但联系得上 n3，n3 照样确认它的日志，所以 n1 仍然是「多数」，照样告诉客户端成功。**3009 条确认过的写入不在新主节点上**。直到 n2 晋升大约 2 秒后，两个主节点轮流改写 n3 的日志，n1 等的确认迟迟不来，它的写入才开始超时。

多数确认防住了「节点崩溃」，防不住「两个节点都以为自己是主节点」：n3 同时为两个主节点投了赞成票。

## 5. 任期号：让旧主节点退位

问题的关键是，n3 不知道应该听谁的。解决办法是给每一任主节点一个递增的**任期号**（epoch，也叫 term、generation、fencing token）：n2 晋升时任期从 1 变成 2。节点只服从任期最大的主节点：

- 从节点收到更高任期的消息，就记下这个任期，此后**拒绝**更低任期的复制请求，回复 `STALE 2`；
- 主节点收到 `STALE`，知道自己过时了，**主动退位**。

```c
} else if (sscanf(line, "REPL %lu %lu %31s %63s", &a, &b, k, v) == 4) {
    if (fence && a < epoch)
        snprintf(out, sizeof out, "STALE %lu\n", epoch);   /* 你是旧的主节点 */
    ...
```

打开任期检查（`-fence 1`），再做一次：

```
partition n1<->n2 at 1793947631.536
1793947632.534 n2 primary silent for 1000 ms: promote myself, epoch 2
1793947632.536 n1 step down, epoch 2
id=n1 role=backup epoch=2 log=3125
client a -> n1: acked 3124, errors 280, first error at 1793947632.537, last ok at 1793947632.535
acked 3124, missing on 10.77.0.12:7000: 1000 (acked between 1793947631.537 and 1793947632.535)
10.77.0.12:7000 keys 7824, 10.77.0.13:7000 keys 8824, only in first 0, only in second 1000
```

n2 晋升后 **2 毫秒**，n2 的复制请求让 n3 知道了任期 2；n1 的下一次复制被 n3 拒绝，n1 立刻退位。此后客户端 a 写 n1 只会收到错误，**脑裂的时间从 2.9 秒缩短到 2 毫秒**。

但是还丢了 **1000 条**：分区之后、n2 晋升之前的那 1 秒内，n1 照常工作，通过 n3 的确认告诉客户端成功了。这些写入在 n1 和 n3 上，**唯独不在 n2 上**，而 n2 成了新的主节点。n3 比 n2 新，却没有当选。

<figure class="diagram">
<svg viewBox="0 0 700 210" role="img" aria-label="检查任期时丢掉的 1000 条：分区（631.536 秒）到 n2 晋升（632.534 秒）之间的 1 秒，n1 照常写入并由 n3 确认，这些写入在 n1、n3 上；n2 晋升后 2 毫秒 n1 退位。新主节点 n2 恰好没有这 1000 条">
  <defs><marker id="lost1000-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="lost1000-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <line x1="60.0" y1="120.0" x2="620.0" y2="120.0" style="stroke:var(--border-2);stroke-width:1.2"/><line x1="207.2" y1="40.0" x2="207.2" y2="150.0" style="stroke:var(--border-2);stroke-width:1.2;stroke-dasharray:4 4"/><text x="207.2" y="168.0" text-anchor="middle" style="fill:var(--danger);font-size:11px">切断 n1—n2</text><line x1="406.8" y1="40.0" x2="406.8" y2="150.0" style="stroke:var(--border-2);stroke-width:1.2;stroke-dasharray:4 4"/><text x="406.8" y="168.0" text-anchor="middle" style="fill:var(--accent);font-size:11px">n2 晋升</text><text x="436.8" y="186.0" text-anchor="start" style="fill:var(--muted);font-size:11px">n1 退位（+2 ms）</text><rect x="207.2" y="96" width="199.8" height="22" rx="3" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="307.1" y="112.0" text-anchor="middle" style="fill:var(--danger);font-size:11px">1000 条：n1、n3 有，n2 没有</text><rect x="60.0" y="96" width="147.2" height="22" rx="3" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="133.6" y="112.0" text-anchor="middle" style="fill:var(--fg);font-size:11px">三个节点都有</text><rect x="407.2" y="96" width="212.8" height="22" rx="3" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="513.6" y="112.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">n1 拒绝写入</text><text x="60.0" y="60.0" text-anchor="start" style="fill:var(--fg);font-size:12px">客户端 a 写 n1，收到 OK 的写入在哪里</text>
  </g>
</svg>
</figure>

这正是第 3 篇最后的结论：**多数确认只有在提升最新的节点时才不丢数据**。而 n2 只知道「我联系不上主节点了」，不知道自己落后了多少。

## 6. 两个从节点同时晋升

如果 n2、n3 都盯着 n1 呢？这次不切网络，而是用 `SIGSTOP` 让 n1 暂停 3 秒（第 2 篇中的「假死」）。三个客户端分别写 n1、n2、n3：

```
STOP n1 at 1793948147.631
1793948148.631 n2 primary silent for 1000 ms: promote myself, epoch 2
1793948148.632 n3 primary silent for 1000 ms: promote myself, epoch 2
CONT n1 at 1793948150.640
1793948150.639 n1 step down, epoch 2
id=n1 role=backup epoch=2 log=4248
id=n2 role=primary epoch=2 log=1046
id=n3 role=primary epoch=2 log=4248
client -> n2: acked 6
client -> n3: acked 3209
acked 6, missing on 10.77.0.13:7000: 6
acked 3209, missing on 10.77.0.12:7000: 3209
```

n2 和 n3 在 1 毫秒之内各自晋升，**任期都是 2**。任期号相同，谁也说服不了谁：n2 的复制请求被 n3 以 `STALE 2` 拒绝，反过来也一样，所以 n1 暂停期间两个主节点都凑不齐多数，写入都在等待。

n1 醒来后，收到两个任期 2 的主节点的复制请求，它都接受了，成了**两个主节点共同的从节点**，为两边都提供了确认。于是 n2 确认了 6 条写入，n3 确认了 3209 条，**各自都不在对方那里**。实验结束时，依然有两个主节点。

任期号能区分「新的」和「旧的」主节点，但区分不了两个「同样新的」。要保证**每个任期最多只有一个主节点**，晋升就不能是自己说了算，而要**投票**：

- 一个节点要成为任期 2 的主节点，必须得到**多数**节点的投票；
- 每个节点在每个任期内**只投一票**。

3 个节点中，n2 和 n3 不可能都得到 2 票，所以任期 2 最多只有一个主节点。再加上第 5 节的教训：

- 投票人只投给**日志至少和自己一样新**的候选人。

这样，像第 5 节中落后的 n2，就得不到 n3 的投票，当选的必然是拥有全部已确认写入的节点。这两条规则，再加上任期号，就是 **Raft** 选举的核心，下一章会从头实现它。

## 7. 四次实验的对比

| 配置 | 脑裂持续时间 | 丢失的确认写入 | 节点间分叉 |
|---|---|---|---|
| 异步，不检查任期 | 2.9 s（直到实验结束） | 3881 | 有 |
| 等多数，不检查任期 | 约 2 s | 3009 | 有 |
| 等多数，检查任期 | 2 ms | 1000（晋升前那 1 秒） | n3 比 n2 多 1000 个键 |
| 等多数，检查任期，两个节点同时晋升 | 直到实验结束 | 6 或 3209（取决于听谁的） | 有 |

<figure class="diagram">
<svg viewBox="0 0 700 200" role="img" aria-label="四次实验中丢失的确认写入：异步、不检查任期 3881 条；等多数、不检查任期 3009 条；等多数、检查任期 1000 条；两个节点同时晋升时，n2 的 6 条或 n3 的 3209 条（取决于以谁为准）">
  <defs><marker id="summary-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="summary-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="200.0" y="32.0" text-anchor="end" style="fill:var(--fg);font-size:12px">异步，不检查任期</text><rect x="210.0" y="16.0" width="232.9" height="22.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="450.9" y="32.0" text-anchor="start" style="fill:var(--fg);font-size:12px">3,881 条</text><text x="520.0" y="32.0" text-anchor="start" style="fill:var(--muted);font-size:11px">脑裂 2.9 s</text><text x="200.0" y="70.0" text-anchor="end" style="fill:var(--fg);font-size:12px">等多数，不检查任期</text><rect x="210.0" y="54.0" width="180.5" height="22.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="398.5" y="70.0" text-anchor="start" style="fill:var(--fg);font-size:12px">3,009 条</text><text x="520.0" y="70.0" text-anchor="start" style="fill:var(--muted);font-size:11px">脑裂约 2 s</text><text x="200.0" y="108.0" text-anchor="end" style="fill:var(--fg);font-size:12px">等多数，检查任期</text><rect x="210.0" y="92.0" width="60.0" height="22.0" rx="2" style="fill:var(--mark);stroke:var(--warn);stroke-width:1.5"/><text x="278.0" y="108.0" text-anchor="start" style="fill:var(--fg);font-size:12px">1,000 条</text><text x="520.0" y="108.0" text-anchor="start" style="fill:var(--muted);font-size:11px">脑裂 2 ms</text><text x="200.0" y="146.0" text-anchor="end" style="fill:var(--fg);font-size:12px">两个节点同时晋升</text><rect x="210.0" y="130.0" width="192.5" height="22.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="410.5" y="146.0" text-anchor="start" style="fill:var(--fg);font-size:12px">3,209 条</text><text x="520.0" y="146.0" text-anchor="start" style="fill:var(--muted);font-size:11px">两个主节点并存</text><text x="20.0" y="188.0" text-anchor="start" style="fill:var(--muted);font-size:11px">每一条都是客户端收到了 OK、新主节点上却没有的写入</text>
  </g>
</svg>
</figure>

实际的系统中，还有几种常见的补救手段：

- **STONITH**（Shoot The Other Node In The Head）：晋升之前，先通过电源管理把旧主节点强制断电，确保它不可能再写；
- **把任期号带到存储层**：每次写共享存储都带上任期号，存储拒绝比见过的更小的任期号，这样即使旧主节点醒来，它的写入也会被拒绝；
- **租约**：主节点的身份有一个有效期，过期前没有续约就自己退位，新主节点要等旧租约过期后才上任（依赖时钟，下一篇讨论）。

但最根本的还是第 6 节说的：由多数投票选出唯一的主节点，并且只选日志最新的节点。

## 8. 常见错误

1. **由单个节点判断主节点挂了，然后自己晋升**。它可能只是和主节点之间的网络断了。
2. **以为等多数确认就不会脑裂**。旧主节点联系得上其他从节点时，照样凑得齐多数（实测丢 3009 条）。
3. **以为任期号能解决一切**。旧主节点会退位，但晋升前那段时间的写入照样会丢（1000 条）；两个节点同时晋升时任期号相同。
4. **晋升时不比较日志**。被提升的可能是落后的节点。
5. **分区恢复后简单地「合并」两边的数据**。两边对同一个键的修改无法自动判断谁对。

## 9. 小结

- 自动故障转移 = 故障检测 + 晋升；故障检测会误判，误判时就会有两个主节点（脑裂）；
- 部分分区时，异步复制下两个主节点并存 2.9 秒，旧主节点确认的 3881 条写入不在新主节点上，三个节点的数据各不相同；
- 等多数确认也挡不住：旧主节点通过另一个从节点凑够了多数，丢了 3009 条；
- 任期号让旧主节点 2 毫秒内就退位，但晋升前 1 秒的 1000 条写入丢了，因为晋升的不是最新的节点；
- 两个从节点同时晋升，任期号都是 2，分不出谁对；
- 解决方法是多数投票、每个任期每人一票、只投给日志够新的候选人，这就是下一章的 Raft。

## 10. 练习

1. 5 个节点，用多数投票选主节点，最多能容忍几个节点故障？如果网络分成 2 个节点和 3 个节点两部分，哪一边能选出主节点？
2. 第 5 节的实验中，如果 n3 也开启 `-auto 1000`，但它没有和 n1 断开，它会晋升吗？会发生什么？
3. 有人建议：「分区恢复后，以任期大的主节点为准，任期小的那边的写入直接丢掉。」这有什么问题？

<details>
<summary>点击查看答案</summary>

1. 多数是 3，最多能容忍 2 个节点故障。分成 2 和 3 两部分时，只有 3 个节点的那一边能凑够 3 票选出主节点；2 个节点的那一边永远凑不够多数，不能选主，也不能确认写入（如果旧主节点在这边，它的写入会因为凑不够多数确认而失败）。所以任何时候最多只有一边在工作。

2. 不会。n3 一直能收到 n1 的心跳回应（UDP 回显），不会认为 n1 挂了。结果和第 5 节一样：n2 晋升，n3 被 n2 告知任期 2，n1 被 n3 拒绝后退位。这说明「是否晋升」取决于每个节点各自看到的网络，不同节点的看法可以不同；用投票的话，n3 认为 n1 还活着时可以拒绝给 n2 投票（Raft 的一个扩展 PreVote 就是这样做的）。

3. 任期小的那边的写入，有些已经告诉客户端成功了（第 3 节中的 3881 条），直接丢掉就违背了承诺，客户端以为成功的数据不见了。正确的做法是不让任期小的那边确认任何写入：要么它凑不够多数（投票加多数确认），要么用租约保证新主节点上任时旧主节点已经停止工作。

</details>
