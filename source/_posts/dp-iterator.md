---
title: 迭代器模式
date: 2026-11-12 10:00:00
updated: 2026-11-12 10:00:00
categories:
  - 设计模式
  - 行为型模式
tags:
  - 设计模式
  - 迭代器
  - STL
  - iterator_traits
  - 迭代器失效
  - 生成器
  - C++20
order: 5
description: 播放器用一个环形缓冲区保存最近 8 个音频帧，音量表、音画同步、调试输出三个调用方都要按时间顺序遍历它。把数组、head、容量交给调用方，每个调用方都要自己算「第 i 个帧在哪个格子」，有一个算错了，打印顺序是 8 9 10 3 4 5 6 7；把内部改成「2 的幂 + 计数器」，调用方要改 3 处。给它写一个符合标准的迭代器后，range-for 和 std::max_element、std::find_if 直接能用，同样的改动调用方 0 处。再实测迭代器分类：同一个迭代器标成 forward，-O0 下 std::lower_bound 每次 2.9 ms，标成 random_access 只要 0.29 µs（-O2 下优化器把差距抹平了，std::list 抹不平）；vector 扩容后的悬空迭代器（ASan 报 heap-use-after-free），环形缓冲区覆盖旧帧后迭代器「没悬空却指错了帧」；按需读包的输入迭代器遍历 FLV 文件，峰值内存从 3.1 MB 降到 0.28 MB，拿到第一个包快 30 多倍。最后看 FFmpeg 的 av_read_frame、av_codec_iterate，内部和外部迭代器，C++20 的 concept、ranges 和协程生成器。
top_group_list: true
disableNunjucks: true
viz: [dp-iterator]
---

> **本文目标**：理解迭代器模式：把「怎么一个一个地访问元素」从容器里拿出来，调用方不用知道容器内部怎么存；会给自己的容器写一个符合标准库要求的迭代器（五个类型别名、`*`、`++`、`==`），让 range-for 和 `<algorithm>` 都能用；知道五种迭代器分类和 `std::iterator_traits`，量出分类对 `std::distance`、`std::lower_bound` 的影响；会复现和排查迭代器失效；会写按需读取的输入迭代器，知道内部迭代器和外部迭代器的区别；了解 C++20 的 iterator concept、ranges 和协程生成器；分清迭代器和组合、访问者。
>
> **前置知识**：上一篇《模板方法模式》；《组合模式》（第 6 节用显式栈遍历树，第 8 节提到过迭代器）、《简单工厂与工厂方法》（`createIterator()` 就是一个工厂方法）；音视频专栏里的封装格式（FLV、MP4 里一个个的包）和音频帧。
>
> **编译环境**：g++/clang++ -std=c++17（第 4 节是 C++20），完整代码在 `projects/dp-lab/22-iterator/`。

## 1. 问题：谁来算下标

### 1.1 把内部交出去

播放器的音频输出线程里有一个环形缓冲区，保存最近送出去的 8 个音频帧：音量表要取它们的峰值，音画同步要在里面找某个时间戳的帧，调试时要把它们打印出来。环形缓冲区写满以后，新的帧覆盖最老的帧，所以「最老的帧」不一定在数组开头：

<figure class="diagram">
<svg viewBox="0 0 648 214" role="img" aria-label="环形缓冲区：内存里格子 0～7 依次是第 8、9、10、3、4、5、6、7 帧，head 等于 3；调用方要按时间顺序拿到第 3 到第 10 帧，第 i 个在格子 (head + i) % 8"><g style="font-family:var(--mono)"><text x="0.0" y="40.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">内存里的格子</text><text x="0.0" y="150.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">调用方要的顺序</text><rect x="110" y="22" width="56" height="30" rx="3" style="fill:var(--warn);fill-opacity:.14;stroke:var(--warn);stroke-width:1.2"/><text x="138.0" y="42.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">第8帧</text><text x="138.0" y="14.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">[0]</text><rect x="176" y="22" width="56" height="30" rx="3" style="fill:var(--warn);fill-opacity:.14;stroke:var(--warn);stroke-width:1.2"/><text x="204.0" y="42.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">第9帧</text><text x="204.0" y="14.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">[1]</text><rect x="242" y="22" width="56" height="30" rx="3" style="fill:var(--warn);fill-opacity:.14;stroke:var(--warn);stroke-width:1.2"/><text x="270.0" y="42.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">第10帧</text><text x="270.0" y="14.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">[2]</text><rect x="308" y="22" width="56" height="30" rx="3" style="fill:var(--link);fill-opacity:.14;stroke:var(--link);stroke-width:1.2"/><text x="336.0" y="42.0" text-anchor="middle" style="fill:var(--link);font-size:11.5px;font-weight:700">第3帧</text><text x="336.0" y="14.0" text-anchor="middle" style="fill:var(--link);font-size:10.5px;font-weight:700">[3] head</text><rect x="374" y="22" width="56" height="30" rx="3" style="fill:var(--link);fill-opacity:.14;stroke:var(--link);stroke-width:1.2"/><text x="402.0" y="42.0" text-anchor="middle" style="fill:var(--link);font-size:11.5px;font-weight:700">第4帧</text><text x="402.0" y="14.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">[4]</text><rect x="440" y="22" width="56" height="30" rx="3" style="fill:var(--link);fill-opacity:.14;stroke:var(--link);stroke-width:1.2"/><text x="468.0" y="42.0" text-anchor="middle" style="fill:var(--link);font-size:11.5px;font-weight:700">第5帧</text><text x="468.0" y="14.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">[5]</text><rect x="506" y="22" width="56" height="30" rx="3" style="fill:var(--link);fill-opacity:.14;stroke:var(--link);stroke-width:1.2"/><text x="534.0" y="42.0" text-anchor="middle" style="fill:var(--link);font-size:11.5px;font-weight:700">第6帧</text><text x="534.0" y="14.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">[6]</text><rect x="572" y="22" width="56" height="30" rx="3" style="fill:var(--link);fill-opacity:.14;stroke:var(--link);stroke-width:1.2"/><text x="600.0" y="42.0" text-anchor="middle" style="fill:var(--link);font-size:11.5px;font-weight:700">第7帧</text><text x="600.0" y="14.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">[7]</text><rect x="110" y="132" width="56" height="30" rx="3" style="fill:none;stroke:var(--link);stroke-width:1.2"/><text x="138.0" y="152.0" text-anchor="middle" style="fill:var(--link);font-size:11.5px;font-weight:700">第3帧</text><text x="138.0" y="178.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">i=0</text><line x1="336.0" y1="53" x2="138.0" y2="130" style="stroke:var(--link);stroke-width:1;stroke-opacity:.6"/><rect x="176" y="132" width="56" height="30" rx="3" style="fill:none;stroke:var(--link);stroke-width:1.2"/><text x="204.0" y="152.0" text-anchor="middle" style="fill:var(--link);font-size:11.5px;font-weight:700">第4帧</text><text x="204.0" y="178.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">i=1</text><line x1="402.0" y1="53" x2="204.0" y2="130" style="stroke:var(--link);stroke-width:1;stroke-opacity:.6"/><rect x="242" y="132" width="56" height="30" rx="3" style="fill:none;stroke:var(--link);stroke-width:1.2"/><text x="270.0" y="152.0" text-anchor="middle" style="fill:var(--link);font-size:11.5px;font-weight:700">第5帧</text><text x="270.0" y="178.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">i=2</text><line x1="468.0" y1="53" x2="270.0" y2="130" style="stroke:var(--link);stroke-width:1;stroke-opacity:.6"/><rect x="308" y="132" width="56" height="30" rx="3" style="fill:none;stroke:var(--link);stroke-width:1.2"/><text x="336.0" y="152.0" text-anchor="middle" style="fill:var(--link);font-size:11.5px;font-weight:700">第6帧</text><text x="336.0" y="178.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">i=3</text><line x1="534.0" y1="53" x2="336.0" y2="130" style="stroke:var(--link);stroke-width:1;stroke-opacity:.6"/><rect x="374" y="132" width="56" height="30" rx="3" style="fill:none;stroke:var(--link);stroke-width:1.2"/><text x="402.0" y="152.0" text-anchor="middle" style="fill:var(--link);font-size:11.5px;font-weight:700">第7帧</text><text x="402.0" y="178.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">i=4</text><line x1="600.0" y1="53" x2="402.0" y2="130" style="stroke:var(--link);stroke-width:1;stroke-opacity:.6"/><rect x="440" y="132" width="56" height="30" rx="3" style="fill:none;stroke:var(--warn);stroke-width:1.2"/><text x="468.0" y="152.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">第8帧</text><text x="468.0" y="178.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">i=5</text><line x1="138.0" y1="53" x2="468.0" y2="130" style="stroke:var(--warn);stroke-width:1;stroke-opacity:.6"/><rect x="506" y="132" width="56" height="30" rx="3" style="fill:none;stroke:var(--warn);stroke-width:1.2"/><text x="534.0" y="152.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">第9帧</text><text x="534.0" y="178.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">i=6</text><line x1="204.0" y1="53" x2="534.0" y2="130" style="stroke:var(--warn);stroke-width:1;stroke-opacity:.6"/><rect x="572" y="132" width="56" height="30" rx="3" style="fill:none;stroke:var(--warn);stroke-width:1.2"/><text x="600.0" y="152.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">第10帧</text><text x="600.0" y="178.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">i=7</text><line x1="270.0" y1="53" x2="600.0" y2="130" style="stroke:var(--warn);stroke-width:1;stroke-opacity:.6"/><text x="0.0" y="204.0" text-anchor="start" style="fill:var(--muted);font-size:11px">第 i 个帧在格子 (head + i) % 8。这条换算谁来做：每个调用方各写一遍，还是只写在迭代器里？</text></g></svg>
</figure>

缓冲区只想提供 `push()`，可是三个调用方都要遍历它。最省事的办法是把数组、`head`、容量都交出去，让调用方自己循环：

```cpp
// 《迭代器模式》：环形缓冲区把内部结构交给调用方——每个调用方都要自己算「第 i 个帧在哪个格子」
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

struct AudioFrame {
    std::int64_t pts;   // 以采样为单位的时间戳
    int samples;        // 这一帧有多少个采样
    float peak;         // 这一帧的峰值（0～1）
};

// 最近 N 个音频帧：写满以后，新的帧覆盖最老的帧
class FrameRing {
public:
    explicit FrameRing(int capacity) : buf_(capacity) {}
    void push(const AudioFrame& f) {
        buf_[(head_ + size_) % capacity()] = f;
        if (size_ < capacity()) ++size_;
        else head_ = (head_ + 1) % capacity();   // 满了：覆盖最老的，head 往前走
    }
    // 调用方要遍历，只好把内部全交出去
    const AudioFrame* data() const { return buf_.data(); }
    int capacity() const { return static_cast<int>(buf_.size()); }
    int head() const { return head_; }           // 最老的帧在哪个格子
    int size() const { return size_; }

private:
    std::vector<AudioFrame> buf_;
    int head_ = 0, size_ = 0;
};

// ===== 调用方 =====

// 调用方 1：音量表，取最近这些帧的峰值
float peakLevel(const FrameRing& r) {
    float p = 0;
    for (int i = 0; i < r.size(); ++i)
        p = std::max(p, r.data()[(r.head() + i) % r.capacity()].peak);
    return p;
}

// 调用方 2：音画同步，找第一个 pts >= t 的帧
const AudioFrame* findAt(const FrameRing& r, std::int64_t t) {
    for (int i = 0; i < r.size(); ++i) {
        const AudioFrame& f = r.data()[(r.head() + i) % r.capacity()];
        if (f.pts >= t) return &f;
    }
    return nullptr;
}

// 调用方 3：调试输出——写的人以为数组就是按时间排好的
void dump(const FrameRing& r) {
    std::printf("dump:");
    for (int i = 0; i < r.size(); ++i) std::printf(" %lld", static_cast<long long>(r.data()[i].pts / 1024));
    std::printf("\n");
}

int main() {
    FrameRing ring(8);
    for (int k = 0; k < 11; ++k) ring.push({k * 1024LL, 1024, 0.1f * (k % 7)});   // 11 帧放进 8 个格子
    std::printf("峰值 %.1f\n", peakLevel(ring));
    if (const AudioFrame* f = findAt(ring, 5000)) std::printf("pts >= 5000 的第一帧：第 %lld 帧\n", static_cast<long long>(f->pts / 1024));
    dump(ring);   // 应该是 3 4 5 6 7 8 9 10
}
```

```text
峰值 0.6
pts >= 5000 的第一帧：第 5 帧
dump: 8 9 10 3 4 5 6 7
```

前两个调用方写对了，第三个以为数组就是按时间排好的，打印出来的顺序是乱的。这不是哪个人粗心，而是设计的问题：「第 i 个帧在格子 `(head + i) % capacity` 里」这条知识本该只属于 `FrameRing`，现在每个调用方都得知道，写 3 遍循环就有 3 次机会写错。

### 1.2 改内部要改几处

环形缓冲区常见的改进是：容量取 2 的幂，用 `& mask` 代替 `%`（取模要做除法，按位与只要一条指令）；`head`、`size` 换成两个只增不减的计数器 `read_`、`write_`，`size = write_ - read_`，「满」和「空」不用再特殊处理，以后改成单生产者单消费者的无锁队列也方便。`exposed_v2.cpp` 是改完的版本，类里面变了，交出去的接口也跟着变了：`head()` 没了，换成 `readPos()` 和 `mask()`。于是每个调用方都要改：

```diff
 float peakLevel(const FrameRing& r) {
     float p = 0;
-    for (int i = 0; i < r.size(); ++i)
-        p = std::max(p, r.data()[(r.head() + i) % r.capacity()].peak);
+    for (std::size_t i = 0; i < r.size(); ++i)
+        p = std::max(p, r.data()[(r.readPos() + i) & r.mask()].peak);
     return p;
 }
```

`measure.sh` 用 `diff -U0` 统计（去掉第一行注释和 `main`，按 `===== 调用方 =====` 把文件分成类和调用方两部分）：

```text
== 调用方里自己算下标的地方（出现 data()[ 的行）
  暴露内部版：3 处；迭代器版：0 处
== 暴露内部版：exposed.cpp → exposed_v2.cpp
  class  6 处改动，改写旧代码 8 行，新增 11 行
  caller 3 处改动，改写旧代码 5 行，新增 5 行
```

类里改了 6 处，这是应该的；调用方也改了 3 处、5 行，这就是把内部结构交出去的代价。这里只有 3 个调用方，真实的播放器里，音量表、波形图、回声消除、录音、统计……每多一个，改内部时就多一处要改，而且编译器不一定帮你找到：`dump()` 那种写法，改完照样编译通过。

## 2. 迭代器模式

### 2.1 意图

GoF 的定义：

> 提供一种方法顺序访问一个聚合对象中的各个元素，而又不暴露该对象的内部表示。

大白话：**容器自己最清楚元素怎么存，那就让容器提供一个「游标」对象，调用方只会说「当前这个」「下一个」「到头了没有」**。怎么从当前这个走到下一个，是游标（迭代器）的事。

GoF 原书的写法是一个抽象的 `Iterator`，有 `First()`、`Next()`、`IsDone()`、`CurrentItem()` 四个操作，容器用 `createIterator()` 造一个配套的具体迭代器：

```cpp
// 《迭代器模式》：GoF 原书的写法——抽象的 Iterator（First / Next / IsDone / CurrentItem），容器用 createIterator() 造一个
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

struct AudioFrame { std::int64_t pts; int samples; float peak; };

template <class T>
class Iterator {                                  // 抽象迭代器
public:
    virtual ~Iterator() = default;
    virtual void first() = 0;
    virtual void next() = 0;
    virtual bool isDone() const = 0;
    virtual const T& currentItem() const = 0;
};

class FrameRing {                                 // 具体聚合
public:
    explicit FrameRing(int capacity) : buf_(capacity) {}
    void push(const AudioFrame& f) {
        buf_[(head_ + size_) % cap()] = f;
        if (size_ < cap()) ++size_;
        else head_ = (head_ + 1) % cap();
    }
    std::unique_ptr<Iterator<AudioFrame>> createIterator() const;   // 工厂方法：造一个配套的迭代器

private:
    int cap() const { return static_cast<int>(buf_.size()); }
    std::vector<AudioFrame> buf_;
    int head_ = 0, size_ = 0;
    friend class FrameRingIterator;
};

class FrameRingIterator : public Iterator<AudioFrame> {   // 具体迭代器：只有它知道格子怎么排
public:
    explicit FrameRingIterator(const FrameRing& r) : r_(r) {}
    void first() override { i_ = 0; }
    void next() override { ++i_; }
    bool isDone() const override { return i_ >= r_.size_; }
    const AudioFrame& currentItem() const override { return r_.buf_[(r_.head_ + i_) % r_.cap()]; }

private:
    const FrameRing& r_;
    int i_ = 0;
};

std::unique_ptr<Iterator<AudioFrame>> FrameRing::createIterator() const { return std::make_unique<FrameRingIterator>(*this); }

int main() {
    FrameRing ring(8);
    for (int k = 0; k < 11; ++k) ring.push({k * 1024LL, 1024, 0.1f * (k % 7)});
    auto it = ring.createIterator();
    std::printf("dump:");
    for (it->first(); !it->isDone(); it->next()) std::printf(" %lld", static_cast<long long>(it->currentItem().pts / 1024));
    std::printf("\n");
}
```

```text
dump: 3 4 5 6 7 8 9 10
```

```uml
label: GoF 迭代器模式：Aggregate 接口的 createIterator 返回 Iterator；FrameRing 实现 Aggregate，创建 FrameRingIterator；FrameRingIterator 实现 Iterator，并引用 FrameRing；Client 只依赖两个接口
classes:
  - { id: Agg, name: Aggregate, stereo: interface, x: 20, y: 20, w: 230, ops: ['* createIterator() : Iterator*'] }
  - { id: It, name: Iterator, stereo: interface, x: 470, y: 20, w: 230, ops: ['* first()', '* next()', '* isDone() : bool', '* currentItem() : const T&'] }
  - { id: Ring, name: FrameRing, x: 20, y: 230, w: 230, attrs: ['- buf_ : vector<AudioFrame>', '- head_, size_ : int'], ops: ['+ push(f)', '+ createIterator() : Iterator*'] }
  - { id: RIt, name: FrameRingIterator, x: 470, y: 230, w: 230, attrs: ['- r_ : const FrameRing&', '- i_ : int'], ops: ['+ first()', '+ next()', '+ isDone() : bool', '+ currentItem() : const T&'] }
  - { id: Cl, name: Client, x: 290, y: 110, w: 130 }
rels:
  - { from: Ring, to: Agg, type: realize }
  - { from: RIt, to: It, type: realize }
  - { from: Ring, to: RIt, type: depend, label: '«create»', points: [[250, 262], [470, 262]] }
  - { from: RIt, to: Ring, type: assoc, label: 'r_', points: [[470, 312], [250, 312]] }
  - { from: Cl, to: Agg, type: depend, route: straight }
  - { from: Cl, to: It, type: depend, route: straight }
notes:
  - { x: 20, y: 400, w: 330, text: ['currentItem() {', '  return r_.buf_[(r_.head_ + i_) % cap];', '}', '换算只写在这一个地方'], to: RIt }
```

四个角色：

- **Iterator（迭代器）**：访问元素的接口；
- **ConcreteIterator（具体迭代器）**：记住「走到哪了」，知道容器的内部结构，是容器的「朋友」；
- **Aggregate（聚合）**：创建迭代器的接口；
- **ConcreteAggregate（具体聚合）**：返回配套的具体迭代器。`createIterator()` 是一个工厂方法（《简单工厂与工厂方法》）。

### 2.2 C++ 的写法：begin 和 end

GoF 的写法在 C++ 里很少见：每次遍历都要 `new` 一个迭代器，每个元素都要调几次虚函数，最要紧的是它和标准库不兼容。C++ 标准库把迭代器模式做成了语言的一部分，换了一套「接口」：

| GoF | C++ 标准库 |
| --- | --- |
| `createIterator()` | `begin()` 和 `end()`，按值返回迭代器 |
| `first()` | 迭代器一造出来就在第一个元素上 |
| `next()` | `++it` |
| `isDone()` | `it == end()`：和一个「越过最后一个元素」的迭代器比较 |
| `currentItem()` | `*it`、`it->` |
| 抽象的 `Iterator` 接口 | 没有基类，靠**约定**：有这几个运算、有五个类型别名的类型都算（C++20 用 concept 写成了代码） |

照这个约定给 `FrameRing` 写一个迭代器，三个调用方就可以直接用标准算法了：

```cpp
// 《迭代器模式》：给环形缓冲区写一个迭代器——调用方只用 begin/end，range-for 和 <algorithm> 都能直接用
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <vector>

struct AudioFrame {
    std::int64_t pts;   // 以采样为单位的时间戳
    int samples;        // 这一帧有多少个采样
    float peak;         // 这一帧的峰值（0～1）
};

class FrameRing {
public:
    explicit FrameRing(int capacity) : buf_(capacity) {}
    void push(const AudioFrame& f) {
        buf_[(head_ + size_) % capacity()] = f;
        if (size_ < capacity()) ++size_;
        else head_ = (head_ + 1) % capacity();   // 满了：覆盖最老的，head 往前走
    }
    int capacity() const { return static_cast<int>(buf_.size()); }
    int size() const { return size_; }

    class const_iterator {
    public:
        // 这五个类型让标准库认识这个迭代器（std::iterator_traits 读的就是它们）
        using iterator_category = std::forward_iterator_tag;
        using value_type = AudioFrame;
        using difference_type = std::ptrdiff_t;
        using pointer = const AudioFrame*;
        using reference = const AudioFrame&;

        const_iterator() = default;
        reference operator*() const { return ring_->buf_[(ring_->head_ + i_) % ring_->capacity()]; }
        pointer operator->() const { return &**this; }
        const_iterator& operator++() { ++i_; return *this; }
        const_iterator operator++(int) { const_iterator t = *this; ++i_; return t; }
        friend bool operator==(const const_iterator& a, const const_iterator& b) { return a.i_ == b.i_; }
        friend bool operator!=(const const_iterator& a, const const_iterator& b) { return a.i_ != b.i_; }

    private:
        friend class FrameRing;
        const_iterator(const FrameRing* r, int i) : ring_(r), i_(i) {}
        const FrameRing* ring_ = nullptr;
        int i_ = 0;                              // 逻辑下标：最老的帧是 0
    };
    const_iterator begin() const { return const_iterator(this, 0); }
    const_iterator end() const { return const_iterator(this, size_); }

private:
    std::vector<AudioFrame> buf_;
    int head_ = 0, size_ = 0;
};

// ===== 调用方 =====

// 调用方 1：音量表，取最近这些帧的峰值
float peakLevel(const FrameRing& r) {
    auto it = std::max_element(r.begin(), r.end(), [](const AudioFrame& a, const AudioFrame& b) { return a.peak < b.peak; });
    return it == r.end() ? 0.0f : it->peak;
}

// 调用方 2：音画同步，找第一个 pts >= t 的帧
const AudioFrame* findAt(const FrameRing& r, std::int64_t t) {
    auto it = std::find_if(r.begin(), r.end(), [t](const AudioFrame& f) { return f.pts >= t; });
    return it == r.end() ? nullptr : &*it;
}

// 调用方 3：调试输出
void dump(const FrameRing& r) {
    std::printf("dump:");
    for (const AudioFrame& f : r) std::printf(" %lld", static_cast<long long>(f.pts / 1024));
    std::printf("\n");
}

int main() {
    FrameRing ring(8);
    for (int k = 0; k < 11; ++k) ring.push({k * 1024LL, 1024, 0.1f * (k % 7)});   // 11 帧放进 8 个格子
    std::printf("峰值 %.1f\n", peakLevel(ring));
    if (const AudioFrame* f = findAt(ring, 5000)) std::printf("pts >= 5000 的第一帧：第 %lld 帧\n", static_cast<long long>(f->pts / 1024));
    dump(ring);
    std::printf("一共 %td 帧，其中峰值超过 0.3 的 %td 帧\n", std::distance(ring.begin(), ring.end()),
                std::count_if(ring.begin(), ring.end(), [](const AudioFrame& f) { return f.peak > 0.3f; }));
}
```

```text
峰值 0.6
pts >= 5000 的第一帧：第 5 帧
dump: 3 4 5 6 7 8 9 10
一共 8 帧，其中峰值超过 0.3 的 3 帧
```

要点：

- **五个类型别名**：`iterator_category`、`value_type`、`difference_type`、`pointer`、`reference`。标准库通过 `std::iterator_traits<It>` 读它们，决定算法怎么走（第 3.1 节）。C++17 起不要再继承 `std::iterator`，它被废弃了。
- **迭代器只存「第几个」**（逻辑下标 `i_`），`operator*` 里才换算成格子。`begin()` 是 `i_ = 0`，`end()` 是 `i_ = size_`，是「最后一个的下一个」，不能解引用。
- **默认构造函数**：前向迭代器要求能默认构造（`std::forward_iterator` concept 也查这一条）。
- **只写 `const_iterator`**：调用方只读。要能改元素，再写一个 `iterator`，通常用一个模板加 `bool IsConst` 参数生成两个，避免写两遍。
- `dump()` 的 range-for、`std::max_element`、`std::find_if`、`std::distance`、`std::count_if` 一行都不用知道格子怎么排。

### 2.3 range-for 展开成什么

range-for 只是语法糖。`for (const AudioFrame& f : r) body` 被编译器展开成：

<!-- run: off -->
```cpp
{
    auto&& __range = r;
    auto __begin = __range.begin();      // 有成员 begin() 就用它，否则找自由函数 begin(r)
    auto __end = __range.end();
    for (; __begin != __end; ++__begin) {
        const AudioFrame& f = *__begin;
        body
    }
}
```

所以只要有 `begin()`、`end()`，返回的东西支持 `!=`、`++`、`*`，就能用 range-for，它甚至不要求那五个类型别名（`<algorithm>` 才要求）。整个过程画成时序图：

```uml
label: 时序图：dump 调用 FrameRing 的 begin 和 end 得到两个迭代器；循环里反复调用 it 的 operator!=、operator*、operator++；operator* 内部读 FrameRing 的 buf_
participants:
  - { id: d, name: 'dump()', x: 20, w: 90 }
  - { id: r, name: 'FrameRing', x: 210, w: 110 }
  - { id: i, name: 'it', x: 440, w: 80 }
messages:
  - { from: d, to: r, text: 'begin()', n: 1 }
  - { from: r, to: d, text: 'it{i_ = 0}', type: reply }
  - { from: d, to: r, text: 'end()', n: 2 }
  - { from: r, to: d, text: 'end{i_ = size_}', type: reply }
  - { from: d, to: i, text: 'it != end', n: 3 }
  - { from: d, to: i, text: '*it', n: 4 }
  - { from: i, to: r, text: 'buf_[(head_ + i_) % cap]' }
  - { from: r, to: d, text: 'const AudioFrame&', type: reply }
  - { from: d, to: i, text: '++it（只做 ++i_）', n: 5 }
frags:
  - { from: 4, to: 8, label: 'loop [it != end]' }
```

下面的演示一步步走一遍：迭代器里只有逻辑下标 `i`，绕回数组开头的那一下（`(3 + 5) % 8 = 0`）发生在 `operator*` 里，调用方的循环看不到。另外两个按钮是第 3 节要讲的两种失效。

<div class="dsviz" data-viz="dp-iterator"></div>

### 2.4 再改内部：调用方不动

把迭代器版也改成「2 的幂 + 计数器」（`ring_iter_v2.cpp`）。这次迭代器里存的不再是逻辑下标，而是帧的序号 `pos_`，`begin()` 返回 `read_`，`end()` 返回 `write_`，`operator*` 取 `buf_[pos_ & mask_]`。调用方的三个函数一个字都没动：

```text
== 迭代器版：ring_iter.cpp → ring_iter_v2.cpp
  class  10 处改动，改写旧代码 16 行，新增 17 行
  caller 0 处改动，改写旧代码 0 行，新增 0 行
```

<figure class="diagram">
<svg viewBox="0 0 730 126" role="img" aria-label="把内部改成「2 的幂 + 计数器」时调用方要改的代码：暴露内部版 3 处改动、改写 5 行；迭代器版 0 处"><g style="font-family:var(--mono)"><text x="188.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">调用方要改几处</text><rect x="188.0" y="30.0" width="135.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="329.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">3 处</text><rect x="188.0" y="60.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="195.5" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0 处</text><text x="458.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">调用方改写几行</text><rect x="458.0" y="30.0" width="150.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="614.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">5 行</text><rect x="458.0" y="60.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="465.5" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0 行</text><text x="180.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">暴露内部（3 个调用方）</text><text x="180.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">迭代器（3 个调用方）</text><text x="10.0" y="118.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">measure.sh：diff -U0 去掉第一行注释和 main，按「===== 调用方 =====」分成类和调用方两部分统计；Clang、GCC 结果相同</text></g></svg>
</figure>

类里面改得比暴露内部版还多（10 处、16 行，因为迭代器也在类里），但这些改动都在一个文件、一个类里，改的人就是最懂它的人；调用方 0 处，也不需要重新理解什么。这就是迭代器模式的价值：**遍历的方式跟着容器走，调用方只依赖「能一个一个地拿」这个抽象**。

### 2.5 什么时候值得写

- 自己写的容器要被很多地方遍历：环形缓冲区、对象池、稀疏数组、跳表、包队列、文件里的记录。
- 遍历的顺序不平凡：环形、树的前序/后序（《组合模式》）、按时间戳交错的多路流、跳过空洞。
- 数据不全在内存里，要边读边处理（第 3.5 节）。

容器内部就是一个 `std::vector`、调用方只是读一读时，直接把 `begin()`/`end()` 转发成 vector 的迭代器就够了（`internal_external.cpp` 里的 `PacketQueue` 就是这么写的），不用自己写迭代器类。

## 3. 深入迭代器

### 3.1 五种分类和 iterator_traits

不同容器的迭代器能力不同：单链表只能往前走，双向链表能后退，数组能一下跳到第 n 个。标准库按能力把迭代器分成一层套一层的几类：

<figure class="diagram">
<svg viewBox="0 0 700 252" role="img" aria-label="迭代器的五个分类，一层套一层：input 只能读和往前走；forward 能多遍；bidirectional 能后退；random_access 能跳；contiguous 元素连续"><g style="font-family:var(--mono)"><rect x="0" y="10" width="700" height="210" rx="4" style="fill:var(--link);fill-opacity:.05;stroke:var(--link);stroke-width:1.2"/><text x="10.0" y="28.0" text-anchor="start" style="fill:var(--link);font-size:12px;font-weight:700">input</text><text x="190.0" y="28.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">*it  ++it  ==</text><text x="690.0" y="28.0" text-anchor="end" style="fill:var(--muted);font-size:11px">istream_iterator、FlvReader、av_read_frame 包装</text><rect x="22" y="44" width="656" height="168" rx="4" style="fill:var(--link);fill-opacity:.05;stroke:var(--link);stroke-width:1.2"/><text x="32.0" y="62.0" text-anchor="start" style="fill:var(--link);font-size:12px;font-weight:700">forward</text><text x="212.0" y="62.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">+ 能多遍、能拷贝着用</text><text x="668.0" y="62.0" text-anchor="end" style="fill:var(--muted);font-size:11px">forward_list、unordered_map、本文的 FrameRing</text><rect x="44" y="78" width="612" height="126" rx="4" style="fill:var(--accent);fill-opacity:.05;stroke:var(--accent);stroke-width:1.2"/><text x="54.0" y="96.0" text-anchor="start" style="fill:var(--accent);font-size:12px;font-weight:700">bidirectional</text><text x="234.0" y="96.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">+ --it</text><text x="646.0" y="96.0" text-anchor="end" style="fill:var(--muted);font-size:11px">list、map、set</text><rect x="66" y="112" width="568" height="84" rx="4" style="fill:var(--accent);fill-opacity:.05;stroke:var(--accent);stroke-width:1.2"/><text x="76.0" y="130.0" text-anchor="start" style="fill:var(--accent);font-size:12px;font-weight:700">random_access</text><text x="256.0" y="130.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">+ it+n  it2-it1  it[n]  &lt;</text><text x="624.0" y="130.0" text-anchor="end" style="fill:var(--muted);font-size:11px">deque、本文 bench 里的 Ring</text><rect x="88" y="146" width="524" height="42" rx="4" style="fill:var(--warn);fill-opacity:.05;stroke:var(--warn);stroke-width:1.2"/><text x="98.0" y="164.0" text-anchor="start" style="fill:var(--warn);font-size:12px;font-weight:700">contiguous（C++20）</text><text x="278.0" y="164.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">+ 元素在内存里连续</text><text x="602.0" y="164.0" text-anchor="end" style="fill:var(--muted);font-size:11px">vector、array、string、原始指针</text><text x="0.0" y="242.0" text-anchor="start" style="fill:var(--muted);font-size:11px">外层的要求最少，越往里能做的运算越多；能用 std::sort 的只有 random_access 以内的</text></g></svg>
</figure>

| 分类 | 比上一类多的运算 | 典型 |
| --- | --- | --- |
| input | `*it`（只读）、`++it`、`==`；只能走一遍 | `std::istream_iterator`、第 3.5 节的 `FlvReader` |
| forward | 可以拷贝一份、走多遍，两个副本走到同一处得到同一个元素 | `std::forward_list`、`std::unordered_map` |
| bidirectional | `--it` | `std::list`、`std::map` |
| random_access | `it + n`、`it2 - it1`、`it[n]`、`<` | `std::deque` |
| contiguous（C++20） | 元素在内存里连续，`&*(it + n) == &*it + n` | `std::vector`、`std::array`、原始指针 |

（输出迭代器 `std::back_insert_iterator`、`std::ostream_iterator` 是另一条线：只能写不能读。）

算法通过 `std::iterator_traits<It>::iterator_category` 读到分类，在编译期选最快的实现，这叫**标签分派**（tag dispatch）。`std::distance` 大致是这样写的：

```cpp
// 《迭代器模式》：标签分派——同一个 distance，按迭代器分类在编译期选不同的实现
#include <cstdio>
#include <iterator>
#include <list>
#include <vector>

template <class It>
long myDistance(It first, It last, std::random_access_iterator_tag) {
    std::printf("  [random_access：直接相减] ");
    return static_cast<long>(last - first);              // O(1)
}
template <class It>
long myDistance(It first, It last, std::input_iterator_tag) {   // forward、bidirectional 都派生自 input，会落到这里
    std::printf("  [其他：一步一步数] ");
    long n = 0;
    for (; first != last; ++first) ++n;                   // O(n)
    return n;
}
template <class It>
long myDistance(It first, It last) {
    return myDistance(first, last, typename std::iterator_traits<It>::iterator_category{});
}

int main() {
    std::vector<int> v(1000);
    std::list<int> l(1000);
    std::printf("%ld\n", myDistance(v.begin(), v.end()));
    std::printf("%ld\n", myDistance(l.begin(), l.end()));
}
```

```text
  [random_access：直接相减] 1000
  [其他：一步一步数] 1000
```

分类标签之间是继承关系（`random_access_iterator_tag` 派生自 `bidirectional_iterator_tag`，再派生自 `forward_iterator_tag`、`input_iterator_tag`），所以没有专门重载的分类会落到「最近的祖先」那一版。`std::advance`、`std::lower_bound`、`std::reverse`、`std::rotate` 都是这么做的。

### 3.2 分类标错了会怎样

迭代器的能力是实现出来的，分类是**声明**出来的，两者可以不一致。`bench_category.cpp` 写了一个环形缓冲区迭代器，`+=`、`-`、`[]`、`<` 全都实现了，但用模板参数决定它声明成哪一类，再和存同样数据的 `std::list` 比：

```cpp
// 《迭代器模式》：同一个环形缓冲区迭代器标成三种分类，再和 std::list 比：std::distance / std::lower_bound 差多少（分别用 -O2 和 -O0 编译）
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <list>
#include <random>
#include <vector>

template <class T>
class Ring {
public:
    explicit Ring(std::size_t cap) : buf_(cap) {}   // cap 必须是 2 的幂
    void push(const T& v) {
        buf_[write_++ & (buf_.size() - 1)] = v;
        if (write_ - read_ > buf_.size()) ++read_;
    }

    // Tag 只决定「告诉标准库自己是哪一类」；下面的运算两种都实现了
    template <class Tag>
    class Iter {
    public:
        using iterator_category = Tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using pointer = const T*;
        using reference = const T&;

        Iter() = default;
        Iter(const Ring* r, std::uint64_t p) : r_(r), p_(p) {}
        reference operator*() const { return r_->buf_[p_ & (r_->buf_.size() - 1)]; }
        pointer operator->() const { return &**this; }
        reference operator[](difference_type n) const { return *(*this + n); }
        Iter& operator++() { ++p_; return *this; }
        Iter operator++(int) { Iter t = *this; ++p_; return t; }
        Iter& operator--() { --p_; return *this; }
        Iter operator--(int) { Iter t = *this; --p_; return t; }
        Iter& operator+=(difference_type n) { p_ += n; return *this; }
        Iter& operator-=(difference_type n) { p_ -= n; return *this; }
        friend Iter operator+(Iter a, difference_type n) { return a += n; }
        friend Iter operator+(difference_type n, Iter a) { return a += n; }
        friend Iter operator-(Iter a, difference_type n) { return a -= n; }
        friend difference_type operator-(const Iter& a, const Iter& b) { return static_cast<difference_type>(a.p_ - b.p_); }
        friend bool operator==(const Iter& a, const Iter& b) { return a.p_ == b.p_; }
        friend bool operator!=(const Iter& a, const Iter& b) { return a.p_ != b.p_; }
        friend bool operator<(const Iter& a, const Iter& b) { return a.p_ < b.p_; }
        friend bool operator>(const Iter& a, const Iter& b) { return a.p_ > b.p_; }
        friend bool operator<=(const Iter& a, const Iter& b) { return a.p_ <= b.p_; }
        friend bool operator>=(const Iter& a, const Iter& b) { return a.p_ >= b.p_; }

    private:
        const Ring* r_ = nullptr;
        std::uint64_t p_ = 0;
    };
    template <class Tag> Iter<Tag> begin() const { return {this, read_}; }
    template <class Tag> Iter<Tag> end() const { return {this, write_}; }

private:
    std::vector<T> buf_;
    std::uint64_t read_ = 0, write_ = 0;
};

volatile std::int64_t sink;

template <class F>
double medianUs(F f) {   // 跑 7 遍取中位数，单位微秒
    std::vector<double> t;
    for (int k = 0; k < 7; ++k) {
        auto t0 = std::chrono::steady_clock::now();
        f();
        t.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count());
    }
    std::sort(t.begin(), t.end());
    return t[3];
}

template <class GetBegin, class GetEnd>
void run(GetBegin b, GetEnd e, const std::vector<std::int64_t>& queries, const char* name) {
    double d = medianUs([&] {
        for (int k = 0; k < 10; ++k) sink = std::distance(b(), e());
    }) / 10;
    double lb = medianUs([&] {
        for (std::int64_t q : queries) sink = *std::lower_bound(b(), e(), q);
    }) / queries.size();
    std::printf("%-28s std::distance %11.3f us   std::lower_bound %11.3f us/次\n", name, d, lb);
}

int main() {
    const std::size_t cap = 1 << 20;                   // 约 100 万帧的时间戳
    Ring<std::int64_t> ring(cap);
    for (std::int64_t k = 0; k < static_cast<std::int64_t>(cap + cap / 2); ++k) ring.push(k * 1024);   // 写满后又绕了半圈
    std::mt19937_64 rng(42);
    std::vector<std::int64_t> queries(20);
    for (auto& q : queries) q = static_cast<std::int64_t>(cap / 2 + rng() % cap) * 1024;   // 都在缓冲区里

    std::printf("%zu 个元素\n", static_cast<std::size_t>(std::distance(ring.begin<std::random_access_iterator_tag>(), ring.end<std::random_access_iterator_tag>())));
    using F = std::forward_iterator_tag;
    using B = std::bidirectional_iterator_tag;
    using R = std::random_access_iterator_tag;
    run([&] { return ring.begin<F>(); }, [&] { return ring.end<F>(); }, queries, "Ring 标成 forward");
    run([&] { return ring.begin<B>(); }, [&] { return ring.end<B>(); }, queries, "Ring 标成 bidirectional");
    run([&] { return ring.begin<R>(); }, [&] { return ring.end<R>(); }, queries, "Ring 标成 random_access");

    std::list<std::int64_t> lst(ring.begin<R>(), ring.end<R>());       // 同样的数据放进链表：迭代器是 bidirectional
    run([&] { return lst.begin(); }, [&] { return lst.end(); }, queries, "std::list（bidirectional）");
}
```

先用 `-O2`（单位微秒，2^20 个元素）：

| | Clang -O2 `distance` | Clang -O2 `lower_bound` | GCC -O2 `distance` | GCC -O2 `lower_bound` |
| --- | --- | --- | --- | --- |
| Ring 标成 forward | 0.004 | 0.19 | 0.004 | 0.015 |
| Ring 标成 random_access | 0.004 | 0.19 | 0.000 | 0.017 |
| `std::list` | 892 | 1503 | 0.000 | 939 |

结果出乎意料：**标成 forward 和标成 random_access 一样快**。原因是这个迭代器的 `++` 只是 `++p_`，`std::distance` 按 forward 的实现循环 100 万次 `++p_`，优化器看出这个循环等价于 `last.p_ - first.p_`，直接算出来了；`lower_bound` 里的 `std::advance(it, n)` 也一样变成了加法。`std::list` 的 `++` 是顺着指针走，优化器没法合并，`lower_bound` 每次要走约 100 万个节点，慢了几千倍。

GCC 的 `std::list` 有一格是 0：libstdc++ 给 `std::list` 的迭代器专门重载了 `std::__distance`，当 `last` 是 `end()` 时，直接读链表里存的元素个数，不用数。libc++ 没有这个优化。

再用 `-O0`（调试版，优化器不帮忙）：

<figure class="diagram">
<svg viewBox="0 0 730 186" role="img" aria-label="-O0 编译时，同一个 Ring 迭代器标成 forward、bidirectional、random_access 三种分类，再和 std::list 比：std::lower_bound 每次的耗时"><g style="font-family:var(--mono)"><text x="188.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang -O0（macOS）</text><rect x="188.0" y="30.0" width="132.1" height="20.0" rx="2" style="fill:var(--warn)"/><text x="326.1" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">2862 µs</text><rect x="188.0" y="60.0" width="136.1" height="20.0" rx="2" style="fill:var(--warn)"/><text x="330.1" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">2949 µs</text><rect x="188.0" y="90.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="195.5" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.29 µs</text><rect x="188.0" y="120.0" width="175.7" height="20.0" rx="2" style="fill:var(--danger)"/><text x="369.7" y="135.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">3807 µs</text><text x="458.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC -O0（Ubuntu）</text><rect x="458.0" y="30.0" width="114.5" height="20.0" rx="2" style="fill:var(--warn)"/><text x="578.5" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">2481 µs</text><rect x="458.0" y="60.0" width="115.4" height="20.0" rx="2" style="fill:var(--warn)"/><text x="579.4" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">2500 µs</text><rect x="458.0" y="90.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="465.5" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.115 µs</text><rect x="458.0" y="120.0" width="155.2" height="20.0" rx="2" style="fill:var(--danger)"/><text x="619.2" y="135.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">3362 µs</text><text x="180.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">Ring 标成 forward</text><text x="180.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">Ring 标成 bidirectional</text><text x="180.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">Ring 标成 random_access</text><text x="180.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">std::list</text><text x="10.0" y="178.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench_category：2^20 个时间戳，20 次 std::lower_bound，7 遍取中位数，单位微秒/次</text></g></svg>
</figure>

| | Clang -O0 `distance` | GCC -O0 `distance` |
| --- | --- | --- |
| Ring 标成 forward | 1585 µs | 1425 µs |
| Ring 标成 random_access | 0.013 µs | 0.008 µs |
| `std::list` | 2182 µs | 2130 µs |

同一份迭代器代码，只是 `iterator_category` 写成了 forward，调试版里 `lower_bound` 就从 0.29 µs 变成了 2862 µs（Clang，慢约 1 万倍；GCC 是 0.115 µs 和 2481 µs，慢约 2 万倍）：它真的一步一步地 `++` 了上百万次。调试版慢到这个程度，单元测试会超时，用调试版复现线上问题也会很难受。

分类写得比实际**高**更糟：比如单链表的迭代器声明成 random_access，`std::sort` 会去调用它根本没有的 `it + n`，编译不过还算好的，如果碰巧有个同名的运算语义不对，结果就是错的。

反过来，能力不够的迭代器传给要求高的算法，编译期就会被拦住。对 `std::forward_list` 调 `std::sort`（`sort_forward_ce.cpp`）：

<!-- run: off -->
```cpp
// 《迭代器模式》：故意编译不过——对只有 forward 迭代器的容器调用 std::sort（std::forward_list 也一样）
#include <algorithm>
#include <forward_list>

int main() {
    std::forward_list<long long> pts = {3072, 0, 2048, 1024};
    std::sort(pts.begin(), pts.end());   // sort 要随机访问：it + n、it2 - it1
    pts.sort();                          // 正确做法：容器自己提供的成员函数（归并排序，只改指针）
}
```

Clang 报了 4 个错误、共 138 行，错误都在 `make_heap.h`、`sort.h` 这些库的内部文件里（「两个 `__forward_list_iterator` 不能相减」）；GCC 报 1 个错误、共 24 行，位置也在 `stl_algo.h` 里。都没有直接说「`sort` 要随机访问迭代器」。第 4.1 节的 C++20 写法会好很多。

### 3.3 迭代器失效

迭代器里存的是「怎么找到元素」：`vector` 的迭代器本质上是一个指针，`list` 的是节点指针，我们的环形缓冲区存的是 `this` 加一个序号。容器的结构一变，这些信息就可能过时，这就是**迭代器失效**。最典型的是 `vector` 扩容：

<!-- run: off -->
```cpp
// 《迭代器模式》：迭代器失效——拿着 vector 的迭代器继续 push_back，扩容后迭代器指向已经释放的内存（用 -fsanitize=address 编译）
#include <cstdint>
#include <cstdio>
#include <vector>

struct Packet {
    int stream;             // 0 视频，1 音频
    std::int64_t dts;       // 解码时间戳（毫秒）
    int size;
};

int main() {
    std::vector<Packet> queue;              // 解复用出来、还没送进解码器的包
    queue.reserve(4);
    for (int k = 0; k < 4; ++k) queue.push_back({0, k * 40LL, 1000 + k});

    auto cur = queue.begin();               // 解码线程正在处理的包
    std::printf("cur 指向 dts=%lld，容量 %zu，数据在 %p\n", static_cast<long long>(cur->dts), queue.capacity(),
                static_cast<void*>(queue.data()));

    queue.push_back({1, 0, 300});           // 解复用又读到一个音频包：容量不够，重新分配
    std::printf("push_back 之后容量 %zu，数据在 %p\n", queue.capacity(), static_cast<void*>(queue.data()));

    std::printf("再读 cur->dts：%lld\n", static_cast<long long>(cur->dts));   // 悬空迭代器
}
```

```text
cur 指向 dts=0，容量 4，数据在 0x104bcd100
push_back 之后容量 8，数据在 0x104bcd160
再读 cur->dts：4
```

`push_back` 发现容量不够，分配一块新内存，把元素搬过去，再释放旧内存，`cur` 还指着旧内存。读出来的 `4` 是一个垃圾值，程序没有崩；GCC 读出来的是 `5196749891961840537`。这种 bug 在测试里经常「看起来没事」。用 AddressSanitizer 编译就能当场抓到：

```bash
c++ -std=c++17 -O1 -g -fsanitize=address invalidation.cpp -o invalidation && ./invalidation
```

```text
==76777==ERROR: AddressSanitizer: heap-use-after-free on address 0x608000000128 ...
READ of size 8 at 0x608000000128 thread T0
    #0 0x000100780bfc in main invalidation.cpp:24

0x608000000128 is located 8 bytes inside of 96-byte region [0x608000000120,0x608000000180)
freed by thread T0 here:
    ...
    #10 0x000100780be4 in std::__1::vector<Packet>::push_back(Packet&&) vector.h:466
    #11 0x000100780be4 in main invalidation.cpp:21
previously allocated by thread T0 here:
    ...
    #6 0x000100780d80 in std::__1::vector<Packet>::reserve(unsigned long) vector.h:1079
```

ASan 指出了三件事：在第 24 行读了已经释放的内存，这块内存是第 21 行的 `push_back` 释放的，最初是 `reserve` 分配的。GCC 下结果相同。

两个标准库还有各自的调试模式：

- GCC 的 `-D_GLIBCXX_DEBUG`：容器换成带检查的版本，每个迭代器都登记在容器上，扩容时标记为失效，解引用时报 `Error: attempt to dereference a singular iterator.` 并终止程序。不需要 ASan，但会改变容器的大小，不能和没开这个宏编译的库混着链接。
- libc++ 的 `-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_DEBUG`：检查下标越界、空指针这一类，**不检查迭代器失效**，上面的程序照样输出 `4`。

常见容器什么操作会让迭代器失效：

| 容器 | 插入 | 删除 |
| --- | --- | --- |
| `vector` | 扩容时全部失效；没扩容时插入点之后的失效 | 删除点及之后的失效 |
| `deque` | 两端插入：迭代器全部失效，元素的引用还有效；中间插入：全部失效 | 删两端：只有被删的失效；删中间：全部失效 |
| `list`、`map`、`set` | 都不失效 | 只有被删的那个失效 |
| `unordered_map` | 重新哈希时迭代器全部失效（引用还有效） | 只有被删的那个失效 |

练习 4 是另一种最常见的写法：一边遍历 `vector` 一边 `erase`。

### 3.4 环形缓冲区：没悬空，却指错了

我们的环形缓冲区不会重新分配内存，迭代器永远不会悬空，ASan 也不会报错。但它有另一种失效：写满以后新帧覆盖旧帧，迭代器指的那个格子里换成了别的帧。点上面演示的「写满覆盖」按钮可以看到过程。`overwrite.cpp` 让迭代器存只增不减的序号，解引用之前检查序号是否还在 `[read_, write_]` 之间：

```cpp
// 《迭代器模式》：环形缓冲区写满后覆盖旧帧——迭代器没有悬空，却悄悄指向了另一帧；带序号检查的迭代器能发现
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

struct AudioFrame { std::int64_t pts; int samples; float peak; };

class FrameRing {
public:
    explicit FrameRing(std::size_t cap) : buf_(cap) {}   // cap 取 2 的幂
    void push(const AudioFrame& f) {
        buf_[write_++ & (buf_.size() - 1)] = f;
        if (write_ - read_ > buf_.size()) ++read_;
    }

    class const_iterator {
    public:
        const_iterator(const FrameRing* r, std::uint64_t p) : r_(r), p_(p) {}
        // 还在缓冲区里吗：序号必须在 [read_, write_] 之间（等于 write_ 的是 end）
        bool valid() const { return p_ >= r_->read_ && p_ <= r_->write_; }
        const AudioFrame& operator*() const {
            assert(valid() && "迭代器指向的帧已经被覆盖");     // 调试版检查，发布版（-DNDEBUG）没有开销
            return r_->buf_[p_ & (r_->buf_.size() - 1)];
        }
        const AudioFrame* operator->() const { return &**this; }
        const_iterator& operator++() { ++p_; return *this; }
        bool operator!=(const const_iterator& o) const { return p_ != o.p_; }
        std::uint64_t seq() const { return p_; }

    private:
        const FrameRing* r_;
        std::uint64_t p_;
    };
    const_iterator begin() const { return {this, read_}; }
    const_iterator end() const { return {this, write_}; }
    // 不检查的取法：只看格子，不看序号（很多环形缓冲区就是这么写的）
    const AudioFrame& slot(std::uint64_t seq) const { return buf_[seq & (buf_.size() - 1)]; }

private:
    std::vector<AudioFrame> buf_;
    std::uint64_t read_ = 0, write_ = 0;
};

int main() {
    FrameRing ring(8);
    for (int k = 0; k < 8; ++k) ring.push({k * 1024LL, 1024, 0.1f});
    FrameRing::const_iterator it = ring.begin();               // 渲染线程记住「下一个要播的帧」
    std::printf("it 指向第 %lld 帧，valid=%d\n", static_cast<long long>(it->pts / 1024), it.valid());

    for (int k = 8; k < 11; ++k) ring.push({k * 1024LL, 1024, 0.9f});   // 解码线程又写了 3 帧，覆盖最老的 3 帧
    std::printf("写入 3 帧之后：格子里现在是第 %lld 帧（本来要的是第 %llu 帧），valid=%d\n",
                static_cast<long long>(ring.slot(it.seq()).pts / 1024), static_cast<unsigned long long>(it.seq()), it.valid());
    std::printf("begin() 已经是第 %lld 帧\n", static_cast<long long>(ring.begin()->pts / 1024));
}
```

```text
it 指向第 0 帧，valid=1
写入 3 帧之后：格子里现在是第 8 帧（本来要的是第 0 帧），valid=0
begin() 已经是第 3 帧
```

如果在这时 `*it`，调试版会触发断言：

```text
Assertion failed: (valid() && "迭代器指向的帧已经被覆盖"), function operator*, file overwrite.cpp, line 24.
```

只看格子的写法（`slot()`）会静静地返回第 8 帧：渲染线程以为在播第 0 帧，其实播的是第 8 帧，中间 7 帧被跳过，听起来就是一个「咔」。这类**逻辑失效**工具抓不到，只能靠迭代器自己带上「版本」信息。`_GLIBCXX_DEBUG` 的做法也是这样：容器记住所有发出去的迭代器，结构变了就把它们标成失效。

### 3.5 惰性迭代器：按需读包

迭代器不一定要指向「已经在内存里的元素」。遍历一个 FLV 文件里的包（音视频专栏讲过：FLV 是一个 9 字节的文件头，后面是一个个 tag，每个 tag 有 11 字节的头，里面是类型、数据长度、时间戳），有两种写法：

- **一次全读**：把所有包解析出来放进 `std::vector<Packet>`，再遍历这个 vector。
- **输入迭代器**：`begin()` 时读第一个 tag，`++` 时才读下一个；迭代器里只有一个指向读取器的指针，所有包共用一块缓冲区。

<!-- run: off -->
```cpp
// 《迭代器模式》：遍历 FLV 文件里的包——一次全读进 vector，还是写一个按需读取的输入迭代器（比较内存和首包时间）
//   ffmpeg -i bbb-10s.mp4 -c copy bbb-10s.flv && ./flv_packets bbb-10s.flv
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <new>
#include <vector>

// ---- 统计 operator new：当前占用和峰值 ----
static std::size_t g_now = 0, g_peak = 0, g_count = 0;
void* operator new(std::size_t n) {
    auto* p = static_cast<std::size_t*>(std::malloc(n + 16));
    if (!p) throw std::bad_alloc();
    *p = n;
    g_now += n; g_peak = std::max(g_peak, g_now); ++g_count;
    return reinterpret_cast<char*>(p) + 16;
}
void operator delete(void* q) noexcept {
    if (!q) return;
    auto* p = reinterpret_cast<std::size_t*>(static_cast<char*>(q) - 16);
    g_now -= *p;
    std::free(p);
}
void operator delete(void* q, std::size_t) noexcept { operator delete(q); }

static std::uint32_t be(const unsigned char* p, int n) { std::uint32_t v = 0; while (n--) v = v << 8 | *p++; return v; }

struct PacketView {                        // 一个 FLV tag：8 音频，9 视频，18 脚本
    int type = 0;
    std::uint32_t ts = 0;                  // 毫秒
    bool key = false;                      // 视频关键帧
    const unsigned char* data = nullptr;
    std::size_t size = 0;
};

// 读一个 tag：11 字节头 + 数据 + 4 字节 PreviousTagSize。buf 由调用方提供
static bool readTag(std::FILE* f, std::vector<unsigned char>& buf, PacketView& pkt) {
    unsigned char h[11];
    if (std::fread(h, 1, 11, f) != 11) return false;
    std::size_t n = be(h + 1, 3);
    buf.resize(n);
    if (std::fread(buf.data(), 1, n, f) != n) return false;
    unsigned char prev[4];
    if (std::fread(prev, 1, 4, f) != 4) return false;
    pkt.type = h[0];
    pkt.ts = be(h + 4, 3) | static_cast<std::uint32_t>(h[7]) << 24;
    pkt.key = pkt.type == 9 && n > 0 && (buf[0] >> 4) == 1;
    pkt.data = buf.data();
    pkt.size = n;
    return true;
}
static std::FILE* openFlv(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    unsigned char h[13];                   // 9 字节文件头 + PreviousTagSize0
    if (f && (std::fread(h, 1, 13, f) != 13 || h[0] != 'F' || h[1] != 'L' || h[2] != 'V')) { std::fclose(f); f = nullptr; }
    return f;
}

// ---- 做法一：一次全读出来 ----
struct Packet {
    int type;
    std::uint32_t ts;
    bool key;
    std::vector<unsigned char> data;
};
std::vector<Packet> readAll(const char* path) {
    std::vector<Packet> out;
    std::FILE* f = openFlv(path);
    if (!f) return out;
    std::vector<unsigned char> buf;
    PacketView v;
    while (readTag(f, buf, v)) out.push_back({v.type, v.ts, v.key, std::vector<unsigned char>(v.data, v.data + v.size)});
    std::fclose(f);
    return out;
}

// ---- 做法二：输入迭代器，++ 时才读下一个 tag ----
class FlvReader {
public:
    explicit FlvReader(const char* path) : f_(openFlv(path)) {}
    ~FlvReader() { if (f_) std::fclose(f_); }
    FlvReader(const FlvReader&) = delete;
    FlvReader& operator=(const FlvReader&) = delete;

    class iterator {
    public:
        using iterator_category = std::input_iterator_tag;   // 只能往前走一遍：++ 之后上一个包就没了
        using value_type = PacketView;
        using difference_type = std::ptrdiff_t;
        using pointer = const PacketView*;
        using reference = const PacketView&;

        iterator() = default;                                // end()：r_ 为空
        explicit iterator(FlvReader* r) : r_(r) { if (!r_->next()) r_ = nullptr; }
        reference operator*() const { return r_->cur_; }
        pointer operator->() const { return &r_->cur_; }
        iterator& operator++() { if (!r_->next()) r_ = nullptr; return *this; }
        void operator++(int) { ++*this; }
        friend bool operator==(const iterator& a, const iterator& b) { return a.r_ == b.r_; }
        friend bool operator!=(const iterator& a, const iterator& b) { return a.r_ != b.r_; }

    private:
        FlvReader* r_ = nullptr;
    };
    iterator begin() { return f_ ? iterator(this) : iterator(); }
    iterator end() { return iterator(); }
    long bytesRead() const { return f_ ? std::ftell(f_) : 0; }

private:
    bool next() { return readTag(f_, buf_, cur_); }
    std::FILE* f_;
    std::vector<unsigned char> buf_;                         // 只有一个缓冲区，每个包都复用它
    PacketView cur_;
};

using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }
template <class F> double median7(F f) {
    std::vector<double> t;
    for (int k = 0; k < 7; ++k) t.push_back(f());
    std::sort(t.begin(), t.end());
    return t[3];
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "bbb-10s.flv";
    if (FlvReader probe(path); probe.begin() == probe.end()) { std::printf("打不开 %s（用 ffmpeg -i bbb-10s.mp4 -c copy bbb-10s.flv 生成）\n", path); return 1; }

    // 1. 包数和峰值内存
    {
        g_peak = g_now; std::size_t base = g_now, c0 = g_count;
        auto all = readAll(path);
        int v = 0, a = 0, s = 0;
        for (const Packet& p : all) (p.type == 9 ? v : p.type == 8 ? a : s)++;
        std::printf("一次全读：%zu 个包（视频 %d，音频 %d，脚本 %d），峰值 %zu 字节，new %zu 次\n", all.size(), v, a, s, g_peak - base, g_count - c0);
    }
    {
        g_peak = g_now; std::size_t base = g_now, c0 = g_count;
        FlvReader r(path);
        std::size_t n = 0, bytes = 0;
        for (const PacketView& p : r) { ++n; bytes += p.size; }
        std::printf("输入迭代器：%zu 个包（数据共 %zu 字节），峰值 %zu 字节，new %zu 次\n", n, bytes, g_peak - base, g_count - c0);
    }
    // 2. 拿到第一个包要多久（前面已经把文件读过两遍，都在系统缓存里）
    double eagerFirst = median7([&] { auto t0 = Clock::now(); auto all = readAll(path); volatile int x = all.front().type; (void)x; return ms(t0, Clock::now()); });
    double lazyFirst = median7([&] { auto t0 = Clock::now(); FlvReader r(path); volatile int x = r.begin()->type; (void)x; return ms(t0, Clock::now()); });
    std::printf("拿到第一个包：一次全读 %.3f ms，输入迭代器 %.3f ms\n", eagerFirst, lazyFirst);
    // 3. 只要找到 5 秒之后的第一个关键帧（seek）就停下
    long lazyBytes = 0;
    double eagerSeek = median7([&] {
        auto t0 = Clock::now(); auto all = readAll(path);
        auto it = std::find_if(all.begin(), all.end(), [](const Packet& p) { return p.key && p.ts >= 5000; });
        volatile std::uint32_t x = it->ts; (void)x; return ms(t0, Clock::now()); });
    double lazySeek = median7([&] {
        auto t0 = Clock::now(); FlvReader r(path);
        auto it = std::find_if(r.begin(), r.end(), [](const PacketView& p) { return p.key && p.ts >= 5000; });
        volatile std::uint32_t x = it->ts; (void)x; lazyBytes = r.bytesRead(); return ms(t0, Clock::now()); });
    {
        FlvReader r(path);
        auto it = std::find_if(r.begin(), r.end(), [](const PacketView& p) { return p.key && p.ts >= 5000; });
        std::printf("找 5 秒后的第一个关键帧（ts=%u）：一次全读 %.3f ms，输入迭代器 %.3f ms，只读了 %ld 字节\n", it->ts, eagerSeek, lazySeek, lazyBytes);
    }
}
```

先用 FFmpeg 把测试视频转封装成 FLV（不重新编码，3 000 038 字节）：

```bash
ffmpeg -i bbb-10s.mp4 -c copy bbb-10s.flv
./flv_packets bbb-10s.flv
```

```text
一次全读：714 个包（视频 242，音频 471，脚本 1），峰值 3223321 字节，new 728 次
输入迭代器：714 个包（数据共 2989315 字节），峰值 289569 字节，new 3 次
拿到第一个包：一次全读 0.374 ms，输入迭代器 0.010 ms
找 5 秒后的第一个关键帧（ts=5458）：一次全读 0.366 ms，输入迭代器 0.134 ms，只读了 1027256 字节
```

（视频 242 个 tag 是 240 帧加上 H.264 的参数集和结束标记，音频 471 个是 470 帧加 AAC 的配置。）

<figure class="diagram">
<svg viewBox="0 0 700 126" role="img" aria-label="遍历 FLV 文件的 714 个包：一次全读进 vector 峰值 3.1 MB、new 728 次；输入迭代器峰值 0.28 MB、new 3 次"><g style="font-family:var(--mono)"><text x="158.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">峰值内存 Clang</text><rect x="158.0" y="30.0" width="171.7" height="20.0" rx="2" style="fill:var(--warn)"/><text x="335.7" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">3148 KB</text><rect x="158.0" y="60.0" width="15.4" height="20.0" rx="2" style="fill:var(--accent)"/><text x="179.4" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">283 KB</text><text x="428.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">峰值内存 GCC</text><rect x="428.0" y="30.0" width="171.2" height="20.0" rx="2" style="fill:var(--warn)"/><text x="605.2" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">3139 KB</text><rect x="428.0" y="60.0" width="14.9" height="20.0" rx="2" style="fill:var(--accent)"/><text x="448.9" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">274 KB</text><text x="150.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">一次全读进 vector</text><text x="150.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">输入迭代器</text><text x="10.0" y="118.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">flv_packets：bbb-10s.flv（3 000 038 字节），重载 operator new 统计；不含 FILE 自己的缓冲区</text></g></svg>
</figure>

<figure class="diagram">
<svg viewBox="0 0 740 186" role="img" aria-label="拿到第一个包、找 5 秒后第一个关键帧的耗时：一次全读都要读完整个文件；输入迭代器首包快 30 多倍，找关键帧快约 3 倍"><g style="font-family:var(--mono)"><text x="198.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang（macOS）</text><rect x="198.0" y="30.0" width="74.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="278.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">0.37 ms</text><rect x="198.0" y="60.0" width="2.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="206.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.01 ms</text><rect x="198.0" y="90.0" width="74.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="278.0" y="105.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">0.37 ms</text><rect x="198.0" y="120.0" width="26.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="230.0" y="135.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.13 ms</text><text x="468.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC（Ubuntu，Docker）</text><rect x="468.0" y="30.0" width="166.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="640.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">0.83 ms</text><rect x="468.0" y="60.0" width="12.4" height="20.0" rx="2" style="fill:var(--accent)"/><text x="486.4" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.062 ms</text><rect x="468.0" y="90.0" width="162.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="636.0" y="105.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">0.81 ms</text><rect x="468.0" y="120.0" width="56.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="530.0" y="135.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.28 ms</text><text x="190.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">全读：拿到第一个包</text><text x="190.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">迭代器：拿到第一个包</text><text x="190.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">全读：找 5 秒后关键帧</text><text x="190.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">迭代器：找 5 秒后关键帧</text><text x="10.0" y="178.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">flv_packets：-O2，每项 7 遍取中位数，文件已在系统缓存里；整个程序跑 3 次取中间那次</text></g></svg>
</figure>

| | Clang（macOS） | GCC（Ubuntu） |
| --- | --- | --- |
| 一次全读：峰值内存 | 3 223 321 字节 | 3 213 911 字节 |
| 输入迭代器：峰值内存 | 289 569 字节 | 280 159 字节 |
| 一次全读：拿到第一个包 | 0.37 ms | 0.83 ms |
| 输入迭代器：拿到第一个包 | 0.010 ms | 0.062 ms |
| 一次全读：找 5 秒后的关键帧 | 0.37 ms | 0.81 ms |
| 输入迭代器：找 5 秒后的关键帧 | 0.13 ms | 0.28 ms |

- **内存**：一次全读要把所有包的数据都复制一份留在内存里（约 299 万字节），再加上读取时用的缓冲区和 714 个 `Packet`，峰值和文件差不多大；输入迭代器的峰值就是最大的那个 tag（一个关键帧，约 28 万字节），和文件长短无关。这个视频只有 10 秒，两小时的电影就是几个 GB 和几百 KB 的区别。
- **首包时间**：一次全读要等整个文件解析完；输入迭代器读一个 tag 就能开始。播放器的「点开就播」靠的就是这个。
- **提前停下**：`std::find_if` 找到 5 秒后的第一个关键帧就返回，后面 2/3 的文件根本没读（只读了 1 027 256 字节）。

代价是**只能走一遍**：`++` 之后上一个包的数据就被覆盖了，所以它是 input 而不是 forward 迭代器。两个副本也不独立（共用一个读取器），不能拿它做 `std::max_element` 这种要回头看「目前最大的那个」的事，要保留的包得自己复制出来。标准库里的 `std::istream_iterator`、`std::filesystem::directory_iterator` 都是这一类。

### 3.6 FFmpeg 里的迭代器

FFmpeg 是 C 库，没有迭代器类，但到处都是迭代器模式，形式是「一个函数 + 一个记住进度的状态」：

| 接口 | 状态放在哪 | 结束标志 |
| --- | --- | --- |
| `av_read_frame(fmt, pkt)`：读下一个包 | `AVFormatContext` 里的文件位置 | 返回负数（`AVERROR_EOF`） |
| `av_codec_iterate(&opaque)`：下一个编解码器 | 调用方的 `void* opaque` | 返回 `NULL` |
| `av_demuxer_iterate(&opaque)`、`av_muxer_iterate(&opaque)` | 同上 | 返回 `NULL` |
| `av_dict_iterate(dict, prev)`：字典的下一项 | 上一项的指针 | 返回 `NULL` |

`void* opaque` 就是一个「不透明的迭代器」：调用方只负责保存它、传回去，不知道里面是数组下标还是别的什么，FFmpeg 以后换内部结构，调用方不用改。把其中两个包成 C++ 的范围，就能用 range-for 和标准算法（`ffmpeg/packets.cpp`，需要 FFmpeg）：

<!-- run: off -->
```cpp
// 《迭代器模式》：把 FFmpeg 的两个「迭代」接口包成 C++ 的范围——av_read_frame 读包（输入迭代器）、av_codec_iterate 列编解码器
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <iterator>

// 1. 文件里的包：av_read_frame 每调用一次读出下一个包，读完返回负数——天生就是只能走一遍的输入迭代器
class Packets {
public:
    explicit Packets(AVFormatContext* fmt) : fmt_(fmt), pkt_(av_packet_alloc()) {}
    ~Packets() { av_packet_free(&pkt_); }
    Packets(const Packets&) = delete;
    Packets& operator=(const Packets&) = delete;

    class iterator {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = AVPacket;
        using difference_type = std::ptrdiff_t;
        using pointer = const AVPacket*;
        using reference = const AVPacket&;
        iterator() = default;
        explicit iterator(Packets* p) : p_(p) { ++*this; }
        reference operator*() const { return *p_->pkt_; }
        pointer operator->() const { return p_->pkt_; }
        iterator& operator++() {
            av_packet_unref(p_->pkt_);                          // 上一个包的数据在这里释放
            if (av_read_frame(p_->fmt_, p_->pkt_) < 0) p_ = nullptr;
            return *this;
        }
        void operator++(int) { ++*this; }
        friend bool operator==(const iterator& a, const iterator& b) { return a.p_ == b.p_; }
        friend bool operator!=(const iterator& a, const iterator& b) { return a.p_ != b.p_; }
    private:
        Packets* p_ = nullptr;
    };
    iterator begin() { return iterator(this); }
    iterator end() { return iterator(); }

private:
    AVFormatContext* fmt_;
    AVPacket* pkt_;
};

// 2. 所有编解码器：av_codec_iterate(&opaque) 用一个 void* 记住「走到哪了」，返回 NULL 表示结束
class Codecs {
public:
    class iterator {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = const AVCodec*;
        using difference_type = std::ptrdiff_t;
        using pointer = const AVCodec* const*;
        using reference = const AVCodec* const&;
        iterator() = default;
        explicit iterator(bool) { ++*this; }
        reference operator*() const { return cur_; }
        iterator& operator++() { cur_ = av_codec_iterate(&opaque_); return *this; }
        void operator++(int) { ++*this; }
        friend bool operator==(const iterator& a, const iterator& b) { return a.cur_ == b.cur_; }
        friend bool operator!=(const iterator& a, const iterator& b) { return a.cur_ != b.cur_; }
    private:
        void* opaque_ = nullptr;
        const AVCodec* cur_ = nullptr;
    };
    iterator begin() const { return iterator(true); }
    iterator end() const { return iterator(); }
};

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "../../../../source/media/bbb-10s.mp4";
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path, nullptr, nullptr) < 0 || avformat_find_stream_info(fmt, nullptr) < 0) {
        std::printf("打不开 %s\n", path);
        return 1;
    }
    int count[8] = {}, keys = 0;
    long long bytes = 0;
    {
        Packets packets(fmt);
        for (const AVPacket& p : packets) {
            if (p.stream_index < 8) ++count[p.stream_index];
            if (p.flags & AV_PKT_FLAG_KEY && fmt->streams[p.stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) ++keys;
            bytes += p.size;
        }
    }
    for (unsigned i = 0; i < fmt->nb_streams && i < 8; ++i)
        std::printf("流 %u（%s）：%d 个包\n", i, av_get_media_type_string(fmt->streams[i]->codecpar->codec_type), count[i]);
    std::printf("视频关键帧 %d 个，包数据共 %lld 字节\n", keys, bytes);
    avformat_close_input(&fmt);

    Codecs codecs;
    auto decoders = std::count_if(codecs.begin(), codecs.end(), [](const AVCodec* c) { return av_codec_is_decoder(c); });
    auto h264 = std::find_if(codecs.begin(), codecs.end(), [](const AVCodec* c) { return c->id == AV_CODEC_ID_H264 && av_codec_is_decoder(c); });
    std::printf("这个 FFmpeg 有 %td 个解码器；第一个 H.264 解码器：%s\n", decoders, h264 != codecs.end() ? (*h264)->name : "无");
}
```

```text
流 0（video）：240 个包
流 1（audio）：470 个包
视频关键帧 7 个，包数据共 2986737 字节
这个 FFmpeg 有 527 个解码器；第一个 H.264 解码器：h264
ffprobe 核对：
index=0|codec_type=video|nb_read_packets=240
index=1|codec_type=audio|nb_read_packets=470
```

包数和 `ffprobe` 对得上。解码器个数取决于 FFmpeg 的编译配置：macOS 上 Homebrew 的 FFmpeg 有 527 个，Ubuntu 24.04 的有 526 个。`Packets` 和 `FlvReader` 一样是输入迭代器：`++` 时 `av_packet_unref` 释放上一个包，所以拿到的 `AVPacket&` 只在这一轮循环里有效。

### 3.7 内部迭代器和外部迭代器

到现在为止的迭代器都是**外部迭代器**：调用方拿着迭代器，自己决定什么时候 `++`。另一种是**内部迭代器**：容器提供一个 `forEach(回调)`，自己控制循环，对每个元素调一次回调。

```cpp
// 《迭代器模式》：内部迭代器（forEach + 回调）和外部迭代器（begin/end）——按 dts 交错合并音频、视频两个包队列
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <vector>

struct Packet { char stream; std::int64_t dts; };   // 'V' 视频，'A' 音频；dts 毫秒

class PacketQueue {
public:
    void push(Packet p) { q_.push_back(p); }
    // 内部迭代器：容器自己控制循环，每个元素调用一次 f；f 返回 false 就提前停下
    template <class F>
    void forEach(F f) const {
        for (const Packet& p : q_)
            if (!f(p)) return;
    }
    // 外部迭代器：把「走到哪了」交给调用方
    auto begin() const { return q_.begin(); }
    auto end() const { return q_.end(); }

private:
    std::vector<Packet> q_;
};

int main() {
    PacketQueue video, audio;
    for (int k = 0; k < 4; ++k) video.push({'V', k * 40LL});   // 25 fps
    for (int k = 0; k < 7; ++k) audio.push({'A', k * 21LL});   // 48 kHz、1024 采样一帧

    // 内部迭代器：一个一个地处理很方便，提前停也行
    int n = 0;
    video.forEach([&](const Packet& p) { ++n; return p.dts < 80; });
    std::printf("forEach 处理到 dts >= 80 停下：看了 %d 个包\n", n);

    // 合并两个队列：要同时在两个序列上各走各的，forEach 做不到（它只能一口气走完一个）
    // 外部迭代器：两个迭代器交替前进，std::merge 现成可用
    std::vector<Packet> out;
    std::merge(video.begin(), video.end(), audio.begin(), audio.end(), std::back_inserter(out),
               [](const Packet& a, const Packet& b) { return a.dts < b.dts; });
    std::printf("按 dts 交错：");
    for (const Packet& p : out) std::printf("%c%lld ", p.stream, static_cast<long long>(p.dts));
    std::printf("\n");

    // 只有 forEach 的话：先把一个队列复制出来，再在 forEach 里手工推进另一个
    std::vector<Packet> a2;
    audio.forEach([&](const Packet& p) { a2.push_back(p); return true; });
    std::size_t i = 0;
    std::printf("只用 forEach：");
    video.forEach([&](const Packet& v) {
        for (; i < a2.size() && a2[i].dts < v.dts; ++i) std::printf("%c%lld ", a2[i].stream, static_cast<long long>(a2[i].dts));
        std::printf("%c%lld ", v.stream, static_cast<long long>(v.dts));
        return true;
    });
    for (; i < a2.size(); ++i) std::printf("%c%lld ", a2[i].stream, static_cast<long long>(a2[i].dts));
    std::printf("\n");
}
```

```text
forEach 处理到 dts >= 80 停下：看了 3 个包
按 dts 交错：V0 A0 A21 V40 A42 A63 V80 A84 A105 V120 A126 
只用 forEach：V0 A0 A21 V40 A42 A63 V80 A84 A105 V120 A126
```

| | 内部迭代器 `forEach(f)` | 外部迭代器 `begin()`/`end()` |
| --- | --- | --- |
| 谁控制循环 | 容器 | 调用方 |
| 写容器的人 | 容易：写一个普通的循环（树就写递归） | 难：要把「走到哪了」存进迭代器对象（树要显式栈，练习 3） |
| 提前停下 | 要约定回调的返回值 | `break` |
| 同时走两个序列（合并、比较、zip） | 做不到，只能先复制一个出来 | 自然：两个迭代器各走各的，`std::merge` 现成 |
| 和标准算法配合 | 不能 | 能 |
| 例子 | `std::for_each` 的用法、很多 C 库的 `xxx_foreach(cb, userdata)` | STL 容器、`av_read_frame` |

音视频里「按 dts 交错合并多路流」是封装器每天都在做的事（写 MP4、FLV 时音视频包要交错存放），这正是外部迭代器擅长的。内部迭代器的好处是写容器的一方简单，第 4.2 节的协程生成器能让外部迭代器也写得像内部迭代器一样简单。

## 4. 现代 C++ 写法

### 4.1 C++20：用 concept 检查迭代器

C++20 把「迭代器的约定」写成了 concept：`std::input_iterator`、`std::forward_iterator`、`std::bidirectional_iterator`、`std::random_access_iterator`、`std::contiguous_iterator`。自己写的迭代器可以用 `static_assert` 检查有没有满足，满足了就能接上 `std::ranges` 的算法和 `std::views` 的管道：

<!-- run: flags="-std=c++20" -->
```cpp
// 《迭代器模式》：C++20——用 concept 检查自己写的迭代器，再把环形缓冲区接上 std::views 的管道
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <ranges>
#include <vector>

struct AudioFrame { std::int64_t pts; int samples; float peak; };

class FrameRing {
public:
    explicit FrameRing(std::size_t cap) : buf_(cap) {}   // cap 取 2 的幂
    void push(const AudioFrame& f) {
        buf_[write_++ & (buf_.size() - 1)] = f;
        if (write_ - read_ > buf_.size()) ++read_;
    }
    class const_iterator {
    public:
        using iterator_concept = std::forward_iterator_tag;   // C++20 的 concept 先看这个
        using value_type = AudioFrame;
        using difference_type = std::ptrdiff_t;

        const_iterator() = default;                           // forward_iterator 要求能默认构造
        const_iterator(const FrameRing* r, std::uint64_t p) : r_(r), p_(p) {}
        const AudioFrame& operator*() const { return r_->buf_[p_ & (r_->buf_.size() - 1)]; }
        const_iterator& operator++() { ++p_; return *this; }
        const_iterator operator++(int) { auto t = *this; ++p_; return t; }
        bool operator==(const const_iterator& o) const { return p_ == o.p_; }   // != 由编译器自动生成

    private:
        const FrameRing* r_ = nullptr;
        std::uint64_t p_ = 0;
    };
    const_iterator begin() const { return {this, read_}; }
    const_iterator end() const { return {this, write_}; }

private:
    std::vector<AudioFrame> buf_;
    std::uint64_t read_ = 0, write_ = 0;
};

static_assert(std::forward_iterator<FrameRing::const_iterator>);
static_assert(!std::bidirectional_iterator<FrameRing::const_iterator>);   // 没写 --
static_assert(std::ranges::forward_range<FrameRing>);

int main() {
    FrameRing ring(8);
    for (int k = 0; k < 11; ++k) ring.push({k * 1024LL, 1024, 0.1f * (k % 7)});

    auto loudest = std::ranges::max(ring, {}, &AudioFrame::peak);           // 投影：按 peak 比较
    std::printf("最响的一帧：第 %lld 帧，峰值 %.1f\n", static_cast<long long>(loudest.pts / 1024), loudest.peak);

    // 惰性管道：不生成中间数组，取够 3 个就停
    auto loud = ring | std::views::filter([](const AudioFrame& f) { return f.peak > 0.2f; })
                     | std::views::transform([](const AudioFrame& f) { return f.pts / 1024; })
                     | std::views::take(3);
    std::printf("峰值超过 0.2 的前 3 帧：");
    for (long long k : loud) std::printf("%lld ", k);
    std::printf("\n");
}
```

```text
最响的一帧：第 6 帧，峰值 0.6
峰值超过 0.2 的前 3 帧：3 4 5 
```

和 C++17 版比，有几处变化：

- `iterator_concept` 是 C++20 新增的别名，concept 优先看它；`iterator_category` 留给老的算法用。
- 只要写 `operator==`，`!=` 由编译器自动生成。`pointer`、`reference` 可以不写（`reference` 从 `operator*` 的返回类型推出来）。
- `std::ranges::max(ring, {}, &AudioFrame::peak)`：直接传容器，不用写 `begin()`/`end()`；第三个参数是**投影**，按 `peak` 比较，不用写 lambda。
- `views::filter | views::transform | views::take` 是惰性的：不生成中间数组，`take(3)` 拿够 3 个就停，后面的元素不会被 `filter` 检查。这和第 3.5 节的输入迭代器是同一个思路。

对 `std::forward_list` 调 `std::ranges::sort`（`ranges_sort_ce.cpp`）：Clang 报错 25 行，第一句就是「`std::forward_list<long long> &` does not satisfy `random_access_range`」，再往下说它连 `bidirectional_range` 都不满足；GCC 报错 31 行，说的也是 `random_access_range` 没满足。比第 3.2 节的 C++17 报错（Clang 138 行，错误都在库的内部）清楚得多。

### 4.2 协程生成器

第 3.5 节的 `FlvReader` 要写一个读取器类、一个迭代器类，把「读到哪了」存在成员变量里。C++20 的协程可以把它写成一个普通的循环，每读出一个 tag 就 `co_yield` 一次，函数在这里暂停，下次 `++` 时从这里继续。C++23 有 `std::generator`，但 Apple Clang 21 和 GCC 13 的标准库都还没有，下面自己写一个最小的（浏览器里的编译器没有 `<coroutine>`，这段请在本地用 `-std=c++20` 编译）：

<!-- run: off -->
```cpp
// 《迭代器模式》：C++20 协程写生成器——按需解析内存里的 FLV tag，co_yield 一个就停一下（C++23 才有 std::generator，这里自己写一个最小的）
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <utility>
#include <vector>

template <class T>
class Generator {
public:
    struct promise_type {
        const T* value = nullptr;
        Generator get_return_object() { return Generator(std::coroutine_handle<promise_type>::from_promise(*this)); }
        std::suspend_always initial_suspend() noexcept { return {}; }   // 创建时不跑，等第一次 begin()
        std::suspend_always final_suspend() noexcept { return {}; }
        std::suspend_always yield_value(const T& v) noexcept { value = &v; return {}; }   // 交出一个值就暂停
        void return_void() {}
        void unhandled_exception() {}
    };
    struct sentinel {};
    class iterator {
    public:
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        explicit iterator(std::coroutine_handle<promise_type> h) : h_(h) {}
        const T& operator*() const { return *h_.promise().value; }
        iterator& operator++() { h_.resume(); return *this; }            // ++ 就是「让协程再跑到下一个 co_yield」
        void operator++(int) { ++*this; }
        bool operator==(sentinel) const { return h_.done(); }
    private:
        std::coroutine_handle<promise_type> h_;
    };
    iterator begin() { h_.resume(); return iterator(h_); }
    sentinel end() { return {}; }

    explicit Generator(std::coroutine_handle<promise_type> h) : h_(h) {}
    Generator(Generator&& o) noexcept : h_(std::exchange(o.h_, {})) {}
    ~Generator() { if (h_) h_.destroy(); }

private:
    std::coroutine_handle<promise_type> h_;
};

struct Tag { int type; std::uint32_t ts; std::size_t size; };

// 解析逻辑写成一个普通的循环；状态（读到哪了）就是协程里的局部变量 p
Generator<Tag> flvTags(const std::vector<unsigned char>& file) {
    std::size_t p = 13;                                     // 跳过 9 字节文件头 + PreviousTagSize0
    while (p + 11 <= file.size()) {
        const unsigned char* h = &file[p];
        Tag t{h[0], static_cast<std::uint32_t>(h[4] << 16 | h[5] << 8 | h[6]), static_cast<std::size_t>(h[1] << 16 | h[2] << 8 | h[3])};
        std::printf("  [解析 %zu 处]", p);
        co_yield t;
        p += 11 + t.size + 4;
    }
}

int main() {
    // 一个很小的 FLV：文件头 + 3 个 tag（视频 ts=0、音频 ts=0、视频 ts=40），数据都只有 2 字节
    std::vector<unsigned char> file = {'F', 'L', 'V', 1, 5, 0, 0, 0, 9, 0, 0, 0, 0};
    for (auto [type, ts] : {std::pair{9, 0}, std::pair{8, 0}, std::pair{9, 40}}) {
        unsigned char tag[] = {static_cast<unsigned char>(type), 0, 0, 2, 0, 0, static_cast<unsigned char>(ts), 0, 0, 0, 0, 0x17, 0, 0, 0, 0, 13};
        file.insert(file.end(), std::begin(tag), std::end(tag));
    }
    for (const Tag& t : flvTags(file)) {
        std::printf(" type=%d ts=%u\n", t.type, t.ts);
        if (t.type == 8) { std::printf("找到第一个音频包，停下：后面的 tag 不会再解析\n"); break; }
    }
}
```

```text
  [解析 13 处] type=9 ts=0
  [解析 30 处] type=8 ts=0
找到第一个音频包，停下：后面的 tag 不会再解析
```

`flvTags()` 读起来就是一个解析循环，局部变量 `p` 就是迭代器的状态，编译器把它存在协程帧里。输出说明它是惰性的：`break` 之后第三个 tag 不会被解析。`Generator` 本身是一次写好、到处复用的模板，有了 `std::generator` 就不用自己写了。

代价：协程帧通常在堆上分配（编译器能证明生命周期时可以省掉），每次 `++` 是一次恢复协程，比手写迭代器的 `++` 贵；生成器是 input 范围，只能走一遍。用在「每个元素本身就有 I/O 或解析成本」的地方（读包、读行、遍历目录）很合适，用在环形缓冲区这种每步只是一次加法的地方就不划算了。

### 4.3 怎么选

| 场景 | 写法 |
| --- | --- |
| 容器内部就是标准容器，只读 | 直接转发 `begin()`/`end()`，不写迭代器类 |
| 自己的数据结构（环形缓冲区、池、树） | 手写迭代器类：五个别名 + 运算；C++20 加 `static_assert(std::forward_iterator<...>)` |
| 能力够就把分类写高 | 能 O(1) 跳就写 random_access，算法会自动用快的实现 |
| 边读边处理（文件、网络、解码） | 输入迭代器，或者协程生成器（逻辑复杂时更好写） |
| 对已有的范围做过滤、变换、截取 | C++20 `std::views` 管道，不生成中间数组 |
| 只需要「对每个元素做一件事」，不和别的序列配合 | 内部迭代器 `forEach(f)` 也可以 |
| 跨 C 接口、要隐藏实现 | FFmpeg 那样的 `xxx_iterate(&opaque)` |

## 5. 迭代器和相似的模式

| 模式 | 解决什么 | 和迭代器的关系 |
| --- | --- | --- |
| **迭代器** | 按顺序访问元素，不暴露内部结构 | — |
| 组合 | 把对象组织成树，叶子和容器统一对待 | 组合定义「结构」，迭代器定义「怎么走」；树的前序遍历可以写成迭代器（练习 3） |
| 访问者（第 4 章后面） | 对一组不同类型的元素做一种新操作，不改元素的类 | 迭代器决定**顺序**，访问者决定对每个元素**做什么**；常常一起用：用迭代器走一遍，对每个元素 `accept(visitor)` |
| 工厂方法 | 子类决定创建什么对象 | GoF 写法的 `createIterator()` 本身就是工厂方法，每种容器造自己的迭代器 |
| 观察者 | 数据变了通知订阅者 | 观察者是**推**（数据来了调你），迭代器是**拉**（你要的时候取）；内部迭代器介于两者之间 |
| 生成器（协程） | 用暂停的函数产生一串值 | 实现迭代器的一种方式，状态在协程帧里，不在迭代器对象里 |

《组合模式》第 6 节把递归的 `size()` 改成了显式栈的循环，那段循环其实就是一个「内部迭代器」：走法和操作写在一起。把显式栈搬进迭代器对象，就得到一个外部迭代器，所有标准算法都能直接用在树上，这是练习 3。

## 6. 小结

- 迭代器模式把「怎么一个一个地访问元素」交给容器提供的迭代器，调用方不用知道内部结构。把数组和下标交给调用方，换算写了 3 遍、错了 1 遍；改内部结构时调用方要改 3 处，有了迭代器是 0 处。
- C++ 的迭代器是一套约定：`begin()`/`end()`，`*`、`++`、`==`，五个类型别名。满足了就能用 range-for 和所有 `<algorithm>`；C++20 用 concept 检查，能接上 ranges 和 views。
- 迭代器分为 input、forward、bidirectional、random_access、contiguous，算法按 `iterator_traits` 的分类选实现。分类写低了，-O0 下 `lower_bound` 慢 1 万倍（-O2 下简单的迭代器被优化器救回来，`std::list` 救不回来）；写高了是错误。
- 迭代器失效：`vector` 扩容后所有迭代器悬空，不开工具时读到垃圾也不崩，用 ASan 或 `_GLIBCXX_DEBUG` 抓；环形缓冲区覆盖旧帧是「没悬空却指错了」的逻辑失效，要靠迭代器自己带序号检查。
- 迭代器可以是惰性的：输入迭代器边读边给，FLV 的 714 个包峰值内存从 3.1 MB 降到 0.28 MB，首包快 30 多倍，提前停下时只读 1/3 的文件；代价是只能走一遍。协程生成器能把它写成普通循环。
- 外部迭代器能同时走多个序列（按 dts 合并音视频包），内部迭代器 `forEach` 更好写但不灵活。

## 7. 练习

1. **辨析**：下面这些迭代器各属于哪一类（input / forward / bidirectional / random_access / contiguous）？说出理由。
   - (a) 第 2.2 节的 `FrameRing::const_iterator`（按它现在声明的）；它**最高**能做成哪一类？
   - (b) 第 3.5 节的 `FlvReader::iterator`。
   - (c) `std::map<std::int64_t, Packet>`（按 pts 排序的包索引）的迭代器。
   - (d) `std::unordered_map<int, Stream>` 的迭代器。
   - (e) `std::string` 的迭代器。
   - (f) `std::filesystem::directory_iterator`。
2. **动手**：给环形缓冲区的迭代器加上 `--`，变成双向迭代器，再提供 `rbegin()`/`rend()`，打印最新的 3 帧（从新到旧），并从后往前找最近一个峰值超过 0.4 的帧。
3. **设计**：给《组合模式》里的文件树写一个前序遍历迭代器 `PreorderRange`：内部用显式栈，不用递归；要能用 range-for 遍历整棵树，并用 `std::count_if` 数出文件个数。迭代器还要能告诉调用方当前节点的深度（用来缩进打印）。
4. **找问题**：播放时钟走到 60 ms 时，要把比时钟早 10 ms 以上的包丢掉。下面的代码丢少了：`A10` 还在。找出原因，改对，并说说有没有更好的写法。

   ```cpp
   // 《迭代器模式》练习 4：找问题——遍历包队列时删掉过期的包，结果有的过期包没删掉
   #include <cstdint>
   #include <cstdio>
   #include <vector>

   struct Packet { char stream; std::int64_t dts; };

   int main() {
       std::vector<Packet> queue = {{'V', 0}, {'A', 10}, {'A', 31}, {'V', 40}, {'A', 52}, {'V', 80}, {'A', 94}};
       const std::int64_t clock = 60;   // 播放时钟已经走到 60 ms：比时钟早 10 ms 以上的包来不及解码，丢掉
       for (auto it = queue.begin(); it != queue.end(); ++it)
           if (it->dts < clock - 10) queue.erase(it);
       std::printf("播放时钟 %lld ms，剩下：", static_cast<long long>(clock));
       for (const Packet& p : queue) std::printf("%c%lld ", p.stream, static_cast<long long>(p.dts));
       std::printf("\n");
   }
   ```

   ```text
   播放时钟 60 ms，剩下：A10 V40 A52 V80 A94 
   ```

<details><summary>点击查看答案</summary>

1. 分类：
   - (a) 声明的是 **forward**。它能多遍（两个副本各走各的，互不影响），没写 `--`。它**最高能做成 random_access**：第 `n` 个就是 `buf_[(head_ + n) % cap]`，`it + n`、`it2 - it1` 都是 O(1)，`bench_category.cpp` 里的 `Ring` 就实现了；但不是 contiguous，元素在内存里会绕回开头，不连续。
   - (b) **input**：`++` 时上一个包的缓冲区被覆盖，所有副本共用一个读取器，只能走一遍。
   - (c) **bidirectional**：红黑树的节点能找前驱和后继，但跳到第 n 个要走 n 步。
   - (d) **forward**：标准只保证能往前走（libstdc++、libc++ 都用单链表把元素串起来）。
   - (e) **contiguous**（C++20 起这么分类；C++17 的 `iterator_category` 是 random_access）：`std::string` 的字符保证连续存放。
   - (f) **input**：每次 `++` 是一次系统调用（`readdir`），只能走一遍；两个副本共享同一个目录流。
2. 只要在迭代器里加 `--`，把分类改成 bidirectional，`std::reverse_iterator` 就能包装它，`std::prev` 也能用了：

   ```cpp
   // 《迭代器模式》练习 2：给环形缓冲区的迭代器加上 --，变成双向迭代器，于是 std::reverse_iterator 和 std::prev 都能用了
   #include <algorithm>
   #include <cstddef>
   #include <cstdint>
   #include <cstdio>
   #include <iterator>
   #include <vector>

   struct AudioFrame { std::int64_t pts; int samples; float peak; };

   class FrameRing {
   public:
       explicit FrameRing(std::size_t cap) : buf_(cap) {}   // cap 取 2 的幂
       void push(const AudioFrame& f) {
           buf_[write_++ & (buf_.size() - 1)] = f;
           if (write_ - read_ > buf_.size()) ++read_;
       }
       class const_iterator {
       public:
           using iterator_category = std::bidirectional_iterator_tag;   // 改了分类
           using value_type = AudioFrame;
           using difference_type = std::ptrdiff_t;
           using pointer = const AudioFrame*;
           using reference = const AudioFrame&;

           const_iterator() = default;
           const_iterator(const FrameRing* r, std::uint64_t p) : r_(r), p_(p) {}
           reference operator*() const { return r_->buf_[p_ & (r_->buf_.size() - 1)]; }
           pointer operator->() const { return &**this; }
           const_iterator& operator++() { ++p_; return *this; }
           const_iterator operator++(int) { auto t = *this; ++p_; return t; }
           const_iterator& operator--() { --p_; return *this; }               // 新增
           const_iterator operator--(int) { auto t = *this; --p_; return t; } // 新增
           friend bool operator==(const const_iterator& a, const const_iterator& b) { return a.p_ == b.p_; }
           friend bool operator!=(const const_iterator& a, const const_iterator& b) { return a.p_ != b.p_; }

       private:
           const FrameRing* r_ = nullptr;
           std::uint64_t p_ = 0;
       };
       using const_reverse_iterator = std::reverse_iterator<const_iterator>;   // 新增
       const_iterator begin() const { return {this, read_}; }
       const_iterator end() const { return {this, write_}; }
       const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }
       const_reverse_iterator rend() const { return const_reverse_iterator(begin()); }

   private:
       std::vector<AudioFrame> buf_;
       std::uint64_t read_ = 0, write_ = 0;
   };

   int main() {
       FrameRing ring(8);
       for (int k = 0; k < 11; ++k) ring.push({k * 1024LL, 1024, 0.1f * (k % 7)});
       std::printf("最新的 3 帧（从新到旧）：");
       std::for_each(ring.rbegin(), std::next(ring.rbegin(), 3), [](const AudioFrame& f) { std::printf("%lld ", static_cast<long long>(f.pts / 1024)); });
       std::printf("\n最后一帧：%lld\n", static_cast<long long>(std::prev(ring.end())->pts / 1024));
       // 从后往前找最近一个峰值超过 0.4 的帧
       auto it = std::find_if(ring.rbegin(), ring.rend(), [](const AudioFrame& f) { return f.peak > 0.4f; });
       if (it != ring.rend()) std::printf("最近一个峰值超过 0.4 的帧：%lld\n", static_cast<long long>(it->pts / 1024));
   }
   ```

   ```text
   最新的 3 帧（从新到旧）：10 9 8 
   最后一帧：10
   最近一个峰值超过 0.4 的帧：6
   ```

   `std::reverse_iterator` 的 `*` 是「先 `--` 一份副本再解引用」，所以 `rbegin()` 由 `end()` 构造，解引用时拿到的是最后一个元素。分类写的是 bidirectional，`std::next(rbegin(), 3)` 会走 3 步；如果再加上 `+=`、`-` 等运算写成 random_access，`std::next` 就会变成 O(1)。

3. 栈里存「节点 + 它正在走第几个子节点」。`++` 的规则：当前节点有子节点就进入第一个；没有就弹栈，找到还有下一个兄弟的祖先，进入那个兄弟。栈空了就是 `end()`，深度就是栈的大小减 1：

   ```cpp
   // 《迭代器模式》练习 3：给《组合模式》的文件树写一个前序遍历迭代器——内部用显式栈，调用方用 range-for 和 std::count_if
   #include <algorithm>
   #include <cstddef>
   #include <cstdio>
   #include <iterator>
   #include <memory>
   #include <string>
   #include <utility>
   #include <vector>

   struct Node {
       std::string name;
       long long bytes = 0;                               // 文件大小；文件夹为 0
       std::vector<std::unique_ptr<Node>> children;       // 文件没有子节点
       Node* add(std::string n, long long b = 0) {
           children.push_back(std::make_unique<Node>(Node{std::move(n), b, {}}));
           return children.back().get();
       }
   };

   class PreorderRange {
   public:
       explicit PreorderRange(const Node& root) : root_(&root) {}

       class iterator {
       public:
           using iterator_category = std::forward_iterator_tag;
           using value_type = Node;
           using difference_type = std::ptrdiff_t;
           using pointer = const Node*;
           using reference = const Node&;

           iterator() = default;                          // end：栈是空的
           explicit iterator(const Node* root) { stack_.push_back({root, 0}); }
           reference operator*() const { return *stack_.back().node; }
           pointer operator->() const { return stack_.back().node; }
           int depth() const { return static_cast<int>(stack_.size()) - 1; }
           iterator& operator++() {
               // 有子节点就进入第一个；没有就往上找「还有下一个兄弟」的祖先
               if (!stack_.back().node->children.empty()) { stack_.push_back({stack_.back().node->children[0].get(), 0}); return *this; }
               while (!stack_.empty()) {
                   stack_.pop_back();
                   if (stack_.empty()) break;
                   Frame& parent = stack_.back();
                   if (++parent.next < parent.node->children.size()) { stack_.push_back({parent.node->children[parent.next].get(), 0}); break; }
               }
               return *this;
           }
           iterator operator++(int) { auto t = *this; ++*this; return t; }
           friend bool operator==(const iterator& a, const iterator& b) {
               if (a.stack_.empty() || b.stack_.empty()) return a.stack_.empty() == b.stack_.empty();
               return a.stack_.back().node == b.stack_.back().node;
           }
           friend bool operator!=(const iterator& a, const iterator& b) { return !(a == b); }

       private:
           struct Frame { const Node* node; std::size_t next; };   // next：当前在走第几个子节点
           std::vector<Frame> stack_;
       };
       iterator begin() const { return iterator(root_); }
       iterator end() const { return iterator(); }

   private:
       const Node* root_;
   };

   int main() {
       Node root{"project", 0, {}};
       Node* media = root.add("media");
       media->add("bbb-10s.mp4", 2'000'000);
       media->add("bbb-10s.flv", 3'000'038);
       Node* src = root.add("src");
       src->add("main.cpp", 1'200);
       src->add("ring.h", 900);
       root.add("README.md", 300);

       PreorderRange all(root);
       for (auto it = all.begin(); it != all.end(); ++it)
           std::printf("%*s%s%s\n", it.depth() * 2, "", it->name.c_str(), it->children.empty() ? "" : "/");
       long long total = 0;
       for (const Node& n : all) total += n.bytes;
       auto files = std::count_if(all.begin(), all.end(), [](const Node& n) { return n.children.empty() && n.bytes > 0; });
       std::printf("%td 个文件，共 %lld 字节\n", files, total);
   }
   ```

   ```text
   project/
     media/
       bbb-10s.mp4
       bbb-10s.flv
     src/
       main.cpp
       ring.h
     README.md
   5 个文件，共 5002438 字节
   ```

   和《组合模式》第 6 节的显式栈比：那里走法和操作（累加大小）写在同一个循环里，换一个操作就要再写一遍循环；这里走法封装在迭代器里，任何标准算法都能直接用在树上，树再深也不会栈溢出（栈在堆上的 `vector` 里）。

4. `queue.erase(it)` 之后，`it` 和它后面的所有迭代器都**失效**了（第 3.3 节的表）。在 libc++ 和 libstdc++ 的实际实现里，`it` 恰好指向被删元素后面的那个元素（`A10` 被搬到了 `V0` 的位置），接着循环的 `++it` 又往前走一步，`A10` 就被跳过了；`A31` 被删后 `V40` 也被跳过，好在它本来就不用删。如果最后一个元素也要删，`++it` 会越过 `end()`，那就是越界读了。GCC 的 `-D_GLIBCXX_DEBUG` 能当场抓到：`Error: attempt to increment a singular iterator.`

   改法一：用 `erase` 的返回值（被删元素后面那个元素的迭代器）继续走，删了就不 `++`。改法二更好：erase-remove 惯用法，`std::remove_if` 把要留下的元素一次搬到前面，再一次 `erase` 掉尾巴，整体 O(n)；改法一每删一个都要把后面的元素往前挪一次，最坏 O(n²)。C++20 可以直接写 `std::erase_if(queue, pred)`。

   ```cpp
   // 《迭代器模式》练习 4 答案：用 erase 的返回值继续走，或者干脆用 erase + remove_if（C++20 是 std::erase_if）
   #include <algorithm>
   #include <cstdint>
   #include <cstdio>
   #include <vector>

   struct Packet { char stream; std::int64_t dts; };

   int main() {
       std::vector<Packet> queue = {{'V', 0}, {'A', 10}, {'A', 31}, {'V', 40}, {'A', 52}, {'V', 80}, {'A', 94}};
       const std::int64_t clock = 60;   // 播放时钟已经走到 60 ms：比时钟早 10 ms 以上的包来不及解码，丢掉
       std::vector<Packet> q2 = queue;
       // 改法一：erase 返回被删元素后面那个元素的迭代器；删了就不再 ++
       for (auto it = queue.begin(); it != queue.end();) {
           if (it->dts < clock - 10) it = queue.erase(it);
           else ++it;
       }
       // 改法二：erase-remove 惯用法，一遍搬完，O(n)；上面的写法每删一个都要把后面的元素往前挪，最坏 O(n²)
       q2.erase(std::remove_if(q2.begin(), q2.end(), [&](const Packet& p) { return p.dts < clock - 10; }), q2.end());
       std::printf("erase-remove 剩下 %zu 个包\n", q2.size());
       std::printf("播放时钟 %lld ms，剩下：", static_cast<long long>(clock));
       for (const Packet& p : queue) std::printf("%c%lld ", p.stream, static_cast<long long>(p.dts));
       std::printf("\n");
   }
   ```

   ```text
   erase-remove 剩下 3 个包
   播放时钟 60 ms，剩下：A52 V80 A94 
   ```

</details>
