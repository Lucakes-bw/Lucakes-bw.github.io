---
title: 法定人数：读写都过半
date: 2026-11-14 15:00:00
updated: 2026-11-14 15:00:00
categories:
  - 分布式系统
  - 分布式基础
tags:
  - 分布式系统
  - 复制
  - Quorum
order: 6
description: 不要主节点，每次写 N 个副本中的 W 个，读其中的 R 个，只要 W + R > N，读到的副本中就一定有最新的那个。本文用 3 个副本实测：网络正常时，W=1、R=1 也有 4.84% 的读拿到旧值，W + R > N 的组合一次也没有；当一个副本收不到写入时，W=1、R=1 读到旧值的比例是 33.1%，W=2、R=1 是 31.5%，W=1、R=2（W + R = N）是 2.4%，W=2、R=2 和 W=1、R=3 一次也没有；而 W=3 时 1422 次写入全部失败。最后讨论 W + R > N 仍然不能保证什么，以及本章的问题如何引出 Raft。
top_group_list: true
disableNunjucks: true
viz: [dist-basics]
---

> **本文目标**：理解无主复制和法定人数（quorum）读写；会证明 W + R > N 时读写集合一定有交集；会用实验测出不同 W、R 下读到旧值的比例和延迟；理解 W、R 的取值对可用性的影响；知道法定人数仍然达不到的保证，为下一章的 Raft 做准备。
>
> **前置知识**：本章第 3、5 篇。
>
> **实验环境**：同第 1 篇。n1、n2、n3 是三个对等的副本，没有主节点；读写由客户端 c1 发起。

## 1. 无主复制

前几篇的复制都有一个主节点，所有写入都经过它，主节点一出问题就要故障转移，第 4 篇看到这有多麻烦。另一条路线是**不要主节点**：Amazon 的 Dynamo，以及受它启发的 Cassandra、Riak，让客户端（或者一个协调者）直接读写多个副本：

- 一个键有 N 个副本；
- **写**：把写入发给所有 N 个副本，收到 **W** 个确认就算成功；
- **读**：向 **R** 个副本读，取其中**版本号最大**的值。

版本号可以是第 5 篇的时间戳，这里用写入者递增的序号。kvd 的 `VPUT k v ver` 只在版本号更大时覆盖，`VGET k tag` 返回版本号和值。

## 2. 为什么 W + R > N

如果 W + R > N，写入成功的 W 个副本和读的 R 个副本**一定有至少一个是同一个**（鸽巢原理：两个集合的大小加起来超过了总数，就不可能互不相交）。读到的 R 个值中，至少有一个是最新的，取版本号最大的，就是最新的值。

N = 3 时常用的是 W = 2、R = 2：写多数，读多数。下面的演示中可以改变 N、W、R，看写入集合和读取集合是否总有交集：

<div class="dsviz" data-viz="dist-quorum"></div>

## 3. 实验：网络正常时

`cli quorum W R 10 n1 n2 n3` 运行两个线程：

- 写者不停地写同一个键，版本号 1、2、3……，每次发给全部 3 个副本，收到 W 个确认就记下「最新确认的版本」，然后写下一个；
- 读者不停地读，每次**随机挑 R 个副本**读，取版本号最大的。如果它比读之前就已确认的版本还小，就是一次**读到旧值**。

```c
uint64_t before = atomic_load(&lastAcked), best = 0;   /* 读之前，已经确认到哪个版本 */
unsigned mask = 0;
while (__builtin_popcount(mask) < qR) mask |= 1u << (rand_r(&seed) % qN);   /* 随机 R 个 */
if (groupCall(&g, mask, req, t, qR, readReply, &best) == 0)
    if (best < before) q.stale++;                      /* 读到了旧值 */
```

读只发给 R 个副本，是 Cassandra 的做法：读的数据可能很大，没必要每次都从所有副本读。

给 c1 和三个副本的网卡都加上 2±2 毫秒的随机延迟（`netem delay 2ms 2ms distribution normal`），每种组合运行 10 秒：

| W | R | W + R > N？ | 写 p50 | 写 p99 | 读 p50 | 读 p99 | 读到旧值 |
|---|---|---|---|---|---|---|---|
| 1 | 1 | 否 | 3.17 | 7.32 | 4.48 | 11.18 | **4.84%**（103 次） |
| 2 | 1 | 否 | 4.71 | 9.01 | 4.47 | 10.90 | **0.66%**（14 次） |
| 1 | 2 | 否（等于） | 3.13 | 7.29 | 5.84 | 11.42 | **0.24%**（4 次） |
| 2 | 2 | 是 | 4.76 | 9.28 | 6.07 | 11.84 | 0 |
| 3 | 1 | 是 | 7.10 | 12.71 | 4.62 | 11.03 | 0 |
| 1 | 3 | 是 | 3.16 | 7.49 | 6.61 | 11.90 | 0 |

（延迟单位毫秒。）

即使网络正常、没有任何副本出问题，**W + R ≤ N 时也会读到旧值**：写入同时发给了 3 个副本，但只等最快的 W 个确认就去写下一个了，其余的副本还在路上；读者恰好挑中了还没收到的副本，就读到了旧值。W = 1、R = 1 时最多，约 5%，平均落后 6.8 个版本：netem 的随机延迟会让包乱序，TCP 要等前面的包到齐才能交付后面的，一个副本就会暂时落后好几个版本。W 或 R 增加到 2，比例降到 1% 以下。W + R > N 的三种组合，一次也没有读到旧值。

代价是**延迟**：要等第 W 快的写确认、第 R 快的读回复。W = 3 就要等最慢的那个副本，写的 p50 从 3.2 毫秒变成 7.1 毫秒。

## 4. 实验：一个副本收不到写入

真正的考验是副本出问题的时候。在 n3 上用 `iptables` 丢掉来自写者的包（写者用单独的 IP 10.77.0.22 连接），n3 就收不到任何写入，但读者照样能读它。这模拟了一个副本因为网络问题错过了一段时间的写入：

```bash
iptables -A INPUT -s 10.77.0.22 -j DROP          # 在 n3 上
```

```
$ WRITER_IP=10.77.0.22 cli quorum 1 1 10 n1:7000 n2:7000 n3:7000
writes 2403 (failed 0), reads 2063 (failed 0), stale reads 683 = 33.11%, avg versions behind 1108.27
```

| W | R | W + R > N？ | 写入失败 | 读到旧值 | 平均落后几个版本 |
|---|---|---|---|---|---|
| 1 | 1 | 否 | 0 | **33.11%** | 1108 |
| 2 | 1 | 否 | 0 | **31.52%** | 689 |
| 1 | 2 | 否（等于） | 0 | **2.41%** | 6.8 |
| 2 | 2 | 是 | 0 | 0 | |
| 1 | 3 | 是 | 0 | 0 | |
| 3 | 1 | 是 | **1422 次，全部失败** | 0 | |

<figure class="diagram">
<svg viewBox="0 0 700 240" role="img" aria-label="读到旧值的比例：网络正常时 W=1 R=1 为 4.84%，W=2 R=1 为 0.66%，W=1 R=2 为 0.24%，W+R&gt;N 的组合为 0；一个副本收不到写入时，W=1 R=1 为 33.11%，W=2 R=1 为 31.52%，W=1 R=2 为 2.41%，其余为 0，W=3 的写入全部失败">
  <defs><marker id="stale-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="stale-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="100.0" y="32.0" text-anchor="end" style="fill:var(--fg);font-size:12px">W=1 R=1</text><rect x="110.0" y="14.0" width="36.3" height="11.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="152.3" y="24.0" text-anchor="start" style="fill:var(--muted);font-size:10px">4.84%</text><rect x="110.0" y="27.0" width="248.3" height="11.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="364.3" y="37.0" text-anchor="start" style="fill:var(--fg);font-size:10px">33.11%</text><text x="100.0" y="64.0" text-anchor="end" style="fill:var(--fg);font-size:12px">W=2 R=1</text><rect x="110.0" y="46.0" width="5.0" height="11.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="121.0" y="56.0" text-anchor="start" style="fill:var(--muted);font-size:10px">0.66%</text><rect x="110.0" y="59.0" width="236.4" height="11.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="352.4" y="69.0" text-anchor="start" style="fill:var(--fg);font-size:10px">31.52%</text><text x="100.0" y="96.0" text-anchor="end" style="fill:var(--fg);font-size:12px">W=1 R=2</text><rect x="110.0" y="78.0" width="1.8" height="11.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="117.8" y="88.0" text-anchor="start" style="fill:var(--muted);font-size:10px">0.24%</text><rect x="110.0" y="91.0" width="18.1" height="11.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="134.1" y="101.0" text-anchor="start" style="fill:var(--fg);font-size:10px">2.41%</text><text x="100.0" y="128.0" text-anchor="end" style="fill:var(--fg);font-size:12px">W=2 R=2</text><rect x="110.0" y="110.0" width="1.5" height="11.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="116.0" y="120.0" text-anchor="start" style="fill:var(--muted);font-size:10px">0%</text><rect x="110.0" y="123.0" width="1.5" height="11.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="116.0" y="133.0" text-anchor="start" style="fill:var(--fg);font-size:10px">0%</text><text x="100.0" y="160.0" text-anchor="end" style="fill:var(--fg);font-size:12px">W=1 R=3</text><rect x="110.0" y="142.0" width="1.5" height="11.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="116.0" y="152.0" text-anchor="start" style="fill:var(--muted);font-size:10px">0%</text><rect x="110.0" y="155.0" width="1.5" height="11.0" rx="2" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="116.0" y="165.0" text-anchor="start" style="fill:var(--fg);font-size:10px">0%</text><text x="100.0" y="192.0" text-anchor="end" style="fill:var(--fg);font-size:12px">W=3 R=1</text><rect x="110.0" y="174.0" width="1.5" height="11.0" rx="2" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="116.0" y="184.0" text-anchor="start" style="fill:var(--muted);font-size:10px">0%</text><text x="116.0" y="197.0" text-anchor="start" style="fill:var(--danger);font-size:10px">写入全部失败（1422 次）</text><rect x="440" y="196" width="12" height="10" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="458.0" y="205.0" text-anchor="start" style="fill:var(--fg);font-size:11px">网络正常</text><rect x="540" y="196" width="12" height="10" style="fill:var(--panel-2);stroke:var(--danger);stroke-width:2"/><text x="558.0" y="205.0" text-anchor="start" style="fill:var(--fg);font-size:11px">一个副本收不到写入</text><text x="20.0" y="230.0" text-anchor="start" style="fill:var(--muted);font-size:11px">N = 3；读随机挑 R 个副本；W + R &gt; N 的三种组合一次也没有读到旧值</text>
  </g>
</svg>
</figure>

- **R = 1**：随机读一个副本，有 1/3 的概率挑中 n3，它什么都没有，所以约三分之一的读拿到了旧值（实测 33.1% 和 31.5%）。W = 2 也没用，因为 W + R = 3，不大于 N；
- **W = 1、R = 2**：随机读两个副本，挑中 n3 加上 n1 或 n2 的概率是 2/3，这时还有一个正常的副本。但写入只等了 1 个确认，另一个正常副本可能还没收到最新的版本，就会读到旧值，只是旧得不多（平均落后 6.8 个版本），比例也小得多（2.4%）。**W + R = N 时，「几乎总是对的」，但不是总是对的**；
- **W = 2、R = 2**：写入一定在 n1、n2 上（n3 收不到，所以确认的两个必然是 n1、n2），读的两个中至少有一个是 n1 或 n2，一次也没有读到旧值；
- **W = 3**：要所有副本确认，n3 收不到，**每一次写入都失败**。

<figure class="diagram">
<svg viewBox="0 0 700 200" role="img" aria-label="N=3 时的两种情况。左：W=2、R=2，写入确认的是副本 1、2，读的是副本 2、3，交集是副本 2，读到最新的版本。右：W=1、R=1，写入只确认了副本 1，读的是副本 3，没有交集，读到旧值">
  <defs><marker id="overlap-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--fg-2)"/></marker><marker id="overlap-d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker></defs>
  <g style="font-family:var(--mono);font-size:13px">
    <text x="20.0" y="22.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">W=2，R=2：W + R &gt; N</text><rect x="20" y="40" width="90" height="50" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="65.0" y="61.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">副本 1</text><text x="65.0" y="77.1" text-anchor="middle" style="fill:var(--muted);font-size:11px">版本 2</text><rect x="125" y="40" width="90" height="50" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="170.0" y="61.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">副本 2</text><text x="170.0" y="77.1" text-anchor="middle" style="fill:var(--muted);font-size:11px">版本 2</text><rect x="121" y="36" width="98" height="58" rx="8" style="fill:none;stroke:var(--accent);stroke-width:2;stroke-dasharray:5 3"/><rect x="230" y="40" width="90" height="50" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="275.0" y="61.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">副本 3</text><text x="275.0" y="77.1" text-anchor="middle" style="fill:var(--muted);font-size:11px">版本 1</text><rect x="226" y="36" width="98" height="58" rx="8" style="fill:none;stroke:var(--danger);stroke-width:2;stroke-dasharray:5 3"/><text x="20.0" y="120.0" text-anchor="start" style="fill:var(--muted);font-size:11px">实心：写入确认；虚线框：读取</text><text x="20.0" y="146.0" text-anchor="start" style="fill:var(--accent);font-size:12px">读到版本 2：最新</text><text x="20.0" y="170.0" text-anchor="start" style="fill:var(--muted);font-size:11px">交集：副本 2</text><text x="370.0" y="22.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">W=1，R=1：W + R ≤ N</text><rect x="370" y="40" width="90" height="50" rx="6" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="415.0" y="61.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">副本 1</text><text x="415.0" y="77.1" text-anchor="middle" style="fill:var(--muted);font-size:11px">版本 2</text><rect x="475" y="40" width="90" height="50" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="520.0" y="61.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">副本 2</text><text x="520.0" y="77.1" text-anchor="middle" style="fill:var(--muted);font-size:11px">版本 1</text><rect x="580" y="40" width="90" height="50" rx="6" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="625.0" y="61.1" text-anchor="middle" style="fill:var(--fg);font-size:12px">副本 3</text><text x="625.0" y="77.1" text-anchor="middle" style="fill:var(--muted);font-size:11px">版本 1</text><rect x="576" y="36" width="98" height="58" rx="8" style="fill:none;stroke:var(--danger);stroke-width:2;stroke-dasharray:5 3"/><text x="370.0" y="120.0" text-anchor="start" style="fill:var(--muted);font-size:11px">实心：写入确认；虚线框：读取</text><text x="370.0" y="146.0" text-anchor="start" style="fill:var(--danger);font-size:12px">读到版本 1：旧值</text><text x="370.0" y="170.0" text-anchor="start" style="fill:var(--muted);font-size:11px">没有交集</text>
  </g>
</svg>
</figure>

W、R 的选择就是在**一致性、延迟、可用性**之间取舍。N = 3 时 W = 2、R = 2 能容忍一个副本出问题，读写都不受影响；W = 3 一个副本出问题就不能写；R = 3 一个副本出问题就不能读。所以通常取 W 和 R 都是多数（⌊N/2⌋ + 1）。

## 5. W + R > N 仍然不够

法定人数保证读到的副本中有最新**确认**的值，但它不能保证所有你想要的东西：

- **写入失败并不等于没有写入**。W = 2 时只收到 1 个确认，客户端认为失败了，但那个副本已经写进去了。之后的读，可能读到这个「失败」的写入，也可能读不到，取决于挑中了哪些副本；
- **读到新值之后，另一个读可能又读到旧值**。写入正在进行，只到达了一部分副本时，一个读挑中了已经有新值的副本，另一个随后的读挑中了还没收到的副本。两次读的结果「倒退」了。要避免这种情况，读者发现副本之间版本不一致时，要先把新值**写回**落后的副本（读修复），等写回确认后再返回；
- **并发的写入要靠版本号排序**。如果版本号是时间戳，就有第 5 篇 LWW 的问题；
- **错过的写入谁来补**。实验中 n3 错过的写入永远不会自己出现。Dynamo 一类的系统用**读修复**和后台的**反熵**（副本之间比较数据，用默克尔树快速找出差异）慢慢补齐。

还有更多的细节：节点故障时临时把写入存在别的节点上（暗示移交）、宽松的法定人数等，它们提高了可用性，但进一步削弱了保证。Cassandra 和 Dynamo 风格的系统选择的是**最终一致性**：副本最终会一致，但某一时刻读到的可能是旧值。

## 6. 本章小结：问题的清单

这一章测出了分布式系统的几个基本难题：

| 篇 | 现象 | 实测 |
|---|---|---|
| 1 | 超时后不知道请求是否执行，重试造成重复 | 2000 次加一多加 94 次；去重后正好 2000 |
| 2 | 慢和死分不开，故障检测会误判 | 5% 丢包、超时 150 ms，两分钟误判 62 次 |
| 3 | 异步复制丢最后的写入；半同步复制要提升最新的从节点 | 丢 304 条；落后的从节点缺 2 条 |
| 4 | 自动故障转移造成脑裂；任期号挡不住同时晋升 | 丢 3881、3009、1000 条；两个任期 2 的主节点 |
| 5 | 时钟偏差让 LWW 丢掉确认过的写入 | 慢 1 毫秒丢 4.1%，慢 50 毫秒丢 100% |
| 6 | 读写都过半才能读到最新确认的值 | W=1、R=1 时 33% 读到旧值；W=2、R=2 时 0 |

把这些经验组合起来，就是**共识算法**的设计思路：

- 用**多数**确认写入（第 3、6 篇）：任何两个多数一定有交集，已经确认的写入一定能被后来的多数看到；
- 用**多数投票**选出领导者，每个任期每人一票（第 4 篇）：每个任期最多一个领导者；
- 只投给**日志最新**的候选人（第 3、4 篇）：新领导者一定拥有所有已经确认的写入；
- 用**任期号**拒绝旧领导者（第 4 篇），用**日志编号**而不是时钟给写入排序（第 5 篇）；
- 客户端请求带上编号去重（第 1 篇）；
- 所有正确性都**不依赖**故障检测是否准确、时钟是否同步（第 2、5 篇），超时只用来决定何时发起选举。

这就是 Raft。下一章从零实现它，并且用本章的方法（杀进程、切网络、暂停进程）去验证它。

## 7. 常见错误

1. **以为 W + R = N 就够了**。实测 W=1、R=2 在网络正常时有 0.24%、一个副本出问题时有 2.4% 的读拿到旧值。
2. **W 取 N 追求「最安全」**。一个副本出问题，所有写入都失败（1422 次全部失败）。
3. **以为写入失败就没有写进去**。只是确认数不够，部分副本可能已经写入了。
4. **用时间戳当版本号而不考虑时钟偏差**。
5. **以为法定人数读写就是强一致的**。没有读修复，两次读可能「倒退」。

## 8. 小结

- 无主复制：写 N 个副本等 W 个确认，读 R 个副本取最新的；
- W + R > N 时读写集合必然相交，一定能读到最新确认的值；
- 网络正常时，W + R ≤ N 也会读到旧值（W=1、R=1 时 4.84%），W + R > N 时为 0；代价是延迟：W=3 的写 p50 是 7.1 毫秒，W=1 是 3.2 毫秒；
- 一个副本收不到写入时：R=1 读到旧值 33%，W=1、R=2 是 2.4%，W=2、R=2 是 0；W=3 的写入全部失败；
- 法定人数仍然不能防止「读倒退」，也不负责补齐错过的写入；
- 本章的经验组合起来，就是下一章的 Raft。

## 9. 练习

1. N = 5，要求能容忍 2 个副本故障，同时 W + R > N。W、R 可以取哪些值？
2. 第 4 节中 W = 2、R = 1 读到旧值的比例（31.5%）为什么略低于 1/3？
3. N = 3、W = 2、R = 2。一次写入只得到 1 个确认，客户端报告失败。之后的读，可能读到这个值吗？请举例。

<details>
<summary>点击查看答案</summary>

1. 容忍 2 个故障，写要在只剩 3 个副本时还能成功，所以 W ≤ 3；读同理 R ≤ 3。再要求 W + R > 5，即 W + R ≥ 6，只能 W = 3、R = 3。也就是读写都取多数。

2. 读者是在「读之前」记下已确认的版本，读者随机挑到 n3 时，n3 上这个键可能根本不存在（版本 0）或者是很早以前的版本，必然是旧值；挑中 n1、n2 时几乎总是最新的。所以比例应该接近 1/3。略低一点是随机波动：2002 次读中，挑中 n3 的次数服从二项分布，标准差约 21 次，也就是约 1 个百分点；31.5% 在 33.3% 的一到两个标准差之内。

3. 可能。比如那 1 个确认来自 n1，n1 上已经有了新值。之后一次读挑中 n1 和 n2，取版本号最大的，就读到了这个「失败」的写入；另一次读挑中 n2 和 n3，又读不到。所以法定人数下，失败的写入处于「不确定」的状态，客户端应该当作「不知道结果」处理（第 1 篇），而不是「一定没写入」。

</details>
