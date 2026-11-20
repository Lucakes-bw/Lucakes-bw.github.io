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

> **本文目标**：理解迭代器模式——把「如何按顺序访问元素」封装在游标对象里，调用方不依赖容器内部存储；会手写符合 STL 标准的迭代器（五个类型别名、`*`、`++`、`==`），无缝接入 range-for 与 `<algorithm>`；掌握五种迭代器分类与 `std::iterator_traits` 标签分派；理解迭代器失效与环形缓冲区逻辑覆盖坑；了解惰性输入迭代器、C++20 `ranges`/concept 与协程生成器；分清迭代器与组合、访问者、观察者。
>
> **前置知识**：《模板方法模式》《组合模式》《简单工厂与工厂方法》。
>
> **编译环境**：`g++ / clang++ -std=c++17`（第 4 节 `-std=c++20`），完整压测与 FLV/FFmpeg 工程代码见 `projects/dp-lab/22-iterator/`。

## 1. 问题：谁来算下标

### 1.1 把内部交出去

播放器的音频输出线程用环形缓冲区保存最近 8 个音频帧：音量表取峰值、音画同步按时间戳查找、调试输出按时间打印。写满后新帧覆盖最老的帧，最老的帧不一定在下标 0：

<figure class="diagram">
<svg viewBox="0 0 648 214" role="img" aria-label="环形缓冲区：内存里格子 0～7 依次是第 8、9、10、3、4、5、6、7 帧，head 等于 3；调用方要按时间顺序拿到第 3 到第 10 帧，第 i 个在格子 (head + i) % 8"><g style="font-family:var(--mono)"><text x="0.0" y="40.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">内存里的格子</text><text x="0.0" y="150.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">调用方要的顺序</text><rect x="110" y="22" width="56" height="30" rx="3" style="fill:var(--warn);fill-opacity:.14;stroke:var(--warn);stroke-width:1.2"/><text x="138.0" y="42.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">第8帧</text><text x="138.0" y="14.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">[0]</text><rect x="176" y="22" width="56" height="30" rx="3" style="fill:var(--warn);fill-opacity:.14;stroke:var(--warn);stroke-width:1.2"/><text x="204.0" y="42.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">第9帧</text><text x="204.0" y="14.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">[1]</text><rect x="242" y="22" width="56" height="30" rx="3" style="fill:var(--warn);fill-opacity:.14;stroke:var(--warn);stroke-width:1.2"/><text x="270.0" y="42.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">第10帧</text><text x="270.0" y="14.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">[2]</text><rect x="308" y="22" width="56" height="30" rx="3" style="fill:var(--link);fill-opacity:.14;stroke:var(--link);stroke-width:1.2"/><text x="336.0" y="42.0" text-anchor="middle" style="fill:var(--link);font-size:11.5px;font-weight:700">第3帧</text><text x="336.0" y="14.0" text-anchor="middle" style="fill:var(--link);font-size:10.5px;font-weight:700">[3] head</text><rect x="374" y="22" width="56" height="30" rx="3" style="fill:var(--link);fill-opacity:.14;stroke:var(--link);stroke-width:1.2"/><text x="402.0" y="42.0" text-anchor="middle" style="fill:var(--link);font-size:11.5px;font-weight:700">第4帧</text><text x="402.0" y="14.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">[4]</text><rect x="440" y="22" width="56" height="30" rx="3" style="fill:var(--link);fill-opacity:.14;stroke:var(--link);stroke-width:1.2"/><text x="468.0" y="42.0" text-anchor="middle" style="fill:var(--link);font-size:11.5px;font-weight:700">第5帧</text><text x="468.0" y="14.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">[5]</text><rect x="506" y="22" width="56" height="30" rx="3" style="fill:var(--link);fill-opacity:.14;stroke:var(--link);stroke-width:1.2"/><text x="534.0" y="42.0" text-anchor="middle" style="fill:var(--link);font-size:11.5px;font-weight:700">第6帧</text><text x="534.0" y="14.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">[6]</text><rect x="572" y="22" width="56" height="30" rx="3" style="fill:var(--link);fill-opacity:.14;stroke:var(--link);stroke-width:1.2"/><text x="600.0" y="42.0" text-anchor="middle" style="fill:var(--link);font-size:11.5px;font-weight:700">第7帧</text><text x="600.0" y="14.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">[7]</text><rect x="110" y="132" width="56" height="30" rx="3" style="fill:none;stroke:var(--link);stroke-width:1.2"/><text x="138.0" y="152.0" text-anchor="middle" style="fill:var(--link);font-size:11.5px;font-weight:700">第3帧</text><text x="138.0" y="178.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">i=0</text><line x1="336.0" y1="53" x2="138.0" y2="130" style="stroke:var(--link);stroke-width:1;stroke-opacity:.6"/><rect x="176" y="132" width="56" height="30" rx="3" style="fill:none;stroke:var(--link);stroke-width:1.2"/><text x="204.0" y="152.0" text-anchor="middle" style="fill:var(--link);font-size:11.5px;font-weight:700">第4帧</text><text x="204.0" y="178.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">i=1</text><line x1="402.0" y1="53" x2="204.0" y2="130" style="stroke:var(--link);stroke-width:1;stroke-opacity:.6"/><rect x="242" y="132" width="56" height="30" rx="3" style="fill:none;stroke:var(--link);stroke-width:1.2"/><text x="270.0" y="152.0" text-anchor="middle" style="fill:var(--link);font-size:11.5px;font-weight:700">第5帧</text><text x="270.0" y="178.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">i=2</text><line x1="468.0" y1="53" x2="270.0" y2="130" style="stroke:var(--link);stroke-width:1;stroke-opacity:.6"/><rect x="308" y="132" width="56" height="30" rx="3" style="fill:none;stroke:var(--link);stroke-width:1.2"/><text x="336.0" y="152.0" text-anchor="middle" style="fill:var(--link);font-size:11.5px;font-weight:700">第6帧</text><text x="336.0" y="178.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">i=3</text><line x1="534.0" y1="53" x2="336.0" y2="130" style="stroke:var(--link);stroke-width:1;stroke-opacity:.6"/><rect x="374" y="132" width="56" height="30" rx="3" style="fill:none;stroke:var(--link);stroke-width:1.2"/><text x="402.0" y="152.0" text-anchor="middle" style="fill:var(--link);font-size:11.5px;font-weight:700">第7帧</text><text x="402.0" y="178.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">i=4</text><line x1="600.0" y1="53" x2="402.0" y2="130" style="stroke:var(--link);stroke-width:1;stroke-opacity:.6"/><rect x="440" y="132" width="56" height="30" rx="3" style="fill:none;stroke:var(--warn);stroke-width:1.2"/><text x="468.0" y="152.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">第8帧</text><text x="468.0" y="178.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">i=5</text><line x1="138.0" y1="53" x2="468.0" y2="130" style="stroke:var(--warn);stroke-width:1;stroke-opacity:.6"/><rect x="506" y="132" width="56" height="30" rx="3" style="fill:none;stroke:var(--warn);stroke-width:1.2"/><text x="534.0" y="152.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">第9帧</text><text x="534.0" y="178.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">i=6</text><line x1="204.0" y1="53" x2="534.0" y2="130" style="stroke:var(--warn);stroke-width:1;stroke-opacity:.6"/><rect x="572" y="132" width="56" height="30" rx="3" style="fill:none;stroke:var(--warn);stroke-width:1.2"/><text x="600.0" y="152.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">第10帧</text><text x="600.0" y="178.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">i=7</text><line x1="270.0" y1="53" x2="600.0" y2="130" style="stroke:var(--warn);stroke-width:1;stroke-opacity:.6"/><text x="0.0" y="204.0" text-anchor="start" style="fill:var(--muted);font-size:11px">第 i 个帧在格子 (head + i) % 8。这条换算谁来做：每个调用方各写一遍，还是只写在迭代器里？</text></g></svg>
</figure>

把 `data()`、`head()`、`capacity()` 全暴露给调用方，每个调用方都得自己算 `(head + i) % capacity`：

```cpp
// 《迭代器模式》：问题版——环形缓冲区暴露内部数组和 head，调用方各自算下标
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

struct AudioFrame { std::int64_t pts; int samples; float peak; };

class FrameRing {
public:
    explicit FrameRing(int cap) : buf_(cap) {}
    void push(const AudioFrame& f) {
        buf_[(head_ + size_) % capacity()] = f;
        if (size_ < capacity()) ++size_;
        else head_ = (head_ + 1) % capacity();
    }
    const AudioFrame* data() const { return buf_.data(); }
    int capacity() const { return static_cast<int>(buf_.size()); }
    int head() const { return head_; }
    int size() const { return size_; }
private:
    std::vector<AudioFrame> buf_;
    int head_ = 0, size_ = 0;
};

float peakLevel(const FrameRing& r) {            // 调用方 1：音量表
    float p = 0;
    for (int i = 0; i < r.size(); ++i)
        p = std::max(p, r.data()[(r.head() + i) % r.capacity()].peak);
    return p;
}
const AudioFrame* findAt(const FrameRing& r, std::int64_t t) {  // 调用方 2：音画同步
    for (int i = 0; i < r.size(); ++i) {
        const auto& f = r.data()[(r.head() + i) % r.capacity()];
        if (f.pts >= t) return &f;
    }
    return nullptr;
}
void dump(const FrameRing& r) {                  // 调用方 3：以为数组按时间有序，直接写错了
    std::printf("dump:");
    for (int i = 0; i < r.size(); ++i) std::printf(" %lld", static_cast<long long>(r.data()[i].pts / 1024));
    std::printf("\n");
}

int main() {
    FrameRing ring(8);
    for (int k = 0; k < 11; ++k) ring.push({k * 1024LL, 1024, 0.1f * (k % 7)});
    std::printf("峰值 %.1f\n", peakLevel(ring));
    if (const auto* f = findAt(ring, 5000))
        std::printf("pts >= 5000 的第一帧：第 %lld 帧\n", static_cast<long long>(f->pts / 1024));
    dump(ring);                                  // 期望 3 4 5 6 7 8 9 10，实际乱序
}
```

```text
峰值 0.6
pts >= 5000 的第一帧：第 5 帧
dump: 8 9 10 3 4 5 6 7
```

### 1.2 改内部要改几处

如果把 `FrameRing` 内部优化成「2 的幂容量 + `& mask` + 单调递增计数器 `read_ / write_`」（`projects/dp-lab/22-iterator/exposed_v2.cpp`），`head()` 消失了，换成 `readPos()` 和 `mask()`。`measure.sh` 统计显示：**3 个调用方全部要改（3 处改动、改写 5 行）**，而且 `dump()` 那种写错下标的 bug 编译器甚至不会报错。

## 2. 迭代器模式

### 2.1 意图与 GoF vs C++ 写法

> **GoF**：提供一种方法顺序访问一个聚合对象中的各个元素，而又不暴露该对象的内部表示。

大白话：**容器提供一个「游标」对象，调用方只管问「当前元素」「下一个」「到头了吗」，下标换算彻底藏在迭代器内部。**

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

GoF 原书用抽象基类 + 堆分配虚调用（`First/Next/IsDone/CurrentItem`），在 C++ 里既慢又无法对接标准库。**C++ STL 用编译期约定代替了虚基类**：

| GoF 接口 | C++ STL 约定 |
| --- | --- |
| `createIterator()` / `first()` | `begin()`、`end()` 按值返回游标（零堆分配） |
| `next()` / `currentItem()` | `++it`、`*it`、`it->` |
| `isDone()` | `it == end()`（与尾后哨兵比较） |
| 抽象基类 `Iterator` | 5 个类型别名 + 运算符约定（C++20 升格为 concept） |

### 2.2 手写标准 STL 风格迭代器

给 `FrameRing` 写一个符合标准的 `const_iterator`，range-for 和 `<algorithm>` 里的算法立刻全部可用：

```cpp
// 《迭代器模式》：手写标准 STL 风格迭代器，无缝接入 range-for 与 <algorithm>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <vector>

struct AudioFrame { std::int64_t pts; int samples; float peak; };

class FrameRing {
public:
    explicit FrameRing(int cap) : buf_(cap) {}
    void push(const AudioFrame& f) {
        buf_[(head_ + size_) % capacity()] = f;
        if (size_ < capacity()) ++size_;
        else head_ = (head_ + 1) % capacity();
    }
    int capacity() const { return static_cast<int>(buf_.size()); }

    class const_iterator {
    public:
        // 标准库通过 std::iterator_traits 读取这 5 个类型别名
        using iterator_category = std::forward_iterator_tag;
        using value_type        = AudioFrame;
        using difference_type   = std::ptrdiff_t;
        using pointer           = const AudioFrame*;
        using reference         = const AudioFrame&;

        const_iterator() = default;              // forward 迭代器要求默认可构造
        reference operator*() const { return ring_->buf_[(ring_->head_ + i_) % ring_->capacity()]; }
        pointer operator->() const { return &**this; }
        const_iterator& operator++() { ++i_; return *this; }
        const_iterator operator++(int) { auto t = *this; ++i_; return t; }
        friend bool operator==(const const_iterator& a, const const_iterator& b) { return a.i_ == b.i_; }
        friend bool operator!=(const const_iterator& a, const const_iterator& b) { return a.i_ != b.i_; }
    private:
        friend class FrameRing;
        const_iterator(const FrameRing* r, int i) : ring_(r), i_(i) {}
        const FrameRing* ring_ = nullptr;
        int i_ = 0;                              // 只存逻辑下标，解引用时再换算格子
    };
    const_iterator begin() const { return const_iterator(this, 0); }
    const_iterator end()   const { return const_iterator(this, size_); }

private:
    std::vector<AudioFrame> buf_;
    int head_ = 0, size_ = 0;
};

int main() {
    FrameRing ring(8);
    for (int k = 0; k < 11; ++k) ring.push({k * 1024LL, 1024, 0.1f * (k % 7)});

    auto mx = std::max_element(ring.begin(), ring.end(),
                               [](const AudioFrame& a, const AudioFrame& b) { return a.peak < b.peak; });
    auto it = std::find_if(ring.begin(), ring.end(), [](const AudioFrame& f) { return f.pts >= 5000; });
    std::printf("峰值 %.1f\n", mx->peak);
    std::printf("pts >= 5000 的第一帧：第 %lld 帧\n", static_cast<long long>(it->pts / 1024));

    std::printf("dump:");
    for (const AudioFrame& f : ring) std::printf(" %lld", static_cast<long long>(f.pts / 1024));
    std::printf("\n一共 %td 帧，峰值 > 0.3 的 %td 帧\n", std::distance(ring.begin(), ring.end()),
                std::count_if(ring.begin(), ring.end(), [](const AudioFrame& f) { return f.peak > 0.3f; }));
}
```

```text
峰值 0.6
pts >= 5000 的第一帧：第 5 帧
dump: 3 4 5 6 7 8 9 10
一共 8 帧，峰值 > 0.3 的 3 帧
```

### 2.3 range-for 原理、时序图与内部重构对比

range-for 只是编译期展开成 `for (auto it = r.begin(), e = r.end(); it != e; ++it)` 的语法糖。下标绕回数组开头的那一下完全发生在 `operator*` 里，调用方看不到：

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

点击下面的演示观察逻辑下标如何绕回，以及第 3 节要讲的两种失效：

<div class="dsviz" data-viz="dp-iterator"></div>

此时再把 `FrameRing` 内部改成「2 的幂 + 计数器 `read_/write_`」（`projects/dp-lab/22-iterator/ring_iter_v2.cpp`），所有改动都收口在 `FrameRing` 类内部，**调用方 0 处改动**：

<figure class="diagram">
<svg viewBox="0 0 730 126" role="img" aria-label="把内部改成「2 的幂 + 计数器」时调用方要改的代码：暴露内部版 3 处改动、改写 5 行；迭代器版 0 处"><g style="font-family:var(--mono)"><text x="188.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">调用方要改几处</text><rect x="188.0" y="30.0" width="135.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="329.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">3 处</text><rect x="188.0" y="60.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="195.5" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0 处</text><text x="458.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">调用方改写几行</text><rect x="458.0" y="30.0" width="150.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="614.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">5 行</text><rect x="458.0" y="60.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="465.5" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0 行</text><text x="180.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">暴露内部（3 个调用方）</text><text x="180.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">迭代器（3 个调用方）</text><text x="10.0" y="118.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">measure.sh：diff -U0 去掉第一行注释和 main，按「===== 调用方 =====」分成类和调用方两部分统计；Clang、GCC 结果相同</text></g></svg>
</figure>

## 3. 深入迭代器：分类、失效与惰性遍历

### 3.1 五种分类与 `iterator_traits` 标签分派

标准库按能力把迭代器分为逐层增强的五类（输出迭代器另成一条线）：

<figure class="diagram">
<svg viewBox="0 0 700 252" role="img" aria-label="迭代器的五个分类，一层套一层：input 只能读和往前走；forward 能多遍；bidirectional 能后退；random_access 能跳；contiguous 元素连续"><g style="font-family:var(--mono)"><rect x="0" y="10" width="700" height="210" rx="4" style="fill:var(--link);fill-opacity:.05;stroke:var(--link);stroke-width:1.2"/><text x="10.0" y="28.0" text-anchor="start" style="fill:var(--link);font-size:12px;font-weight:700">input</text><text x="190.0" y="28.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">*it  ++it  ==</text><text x="690.0" y="28.0" text-anchor="end" style="fill:var(--muted);font-size:11px">istream_iterator、FlvReader、av_read_frame 包装</text><rect x="22" y="44" width="656" height="168" rx="4" style="fill:var(--link);fill-opacity:.05;stroke:var(--link);stroke-width:1.2"/><text x="32.0" y="62.0" text-anchor="start" style="fill:var(--link);font-size:12px;font-weight:700">forward</text><text x="212.0" y="62.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">+ 能多遍、能拷贝着用</text><text x="668.0" y="62.0" text-anchor="end" style="fill:var(--muted);font-size:11px">forward_list、unordered_map、本文的 FrameRing</text><rect x="44" y="78" width="612" height="126" rx="4" style="fill:var(--accent);fill-opacity:.05;stroke:var(--accent);stroke-width:1.2"/><text x="54.0" y="96.0" text-anchor="start" style="fill:var(--accent);font-size:12px;font-weight:700">bidirectional</text><text x="234.0" y="96.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">+ --it</text><text x="646.0" y="96.0" text-anchor="end" style="fill:var(--muted);font-size:11px">list、map、set</text><rect x="66" y="112" width="568" height="84" rx="4" style="fill:var(--accent);fill-opacity:.05;stroke:var(--accent);stroke-width:1.2"/><text x="76.0" y="130.0" text-anchor="start" style="fill:var(--accent);font-size:12px;font-weight:700">random_access</text><text x="256.0" y="130.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">+ it+n  it2-it1  it[n]  &lt;</text><text x="624.0" y="130.0" text-anchor="end" style="fill:var(--muted);font-size:11px">deque、本文 bench 里的 Ring</text><rect x="88" y="146" width="524" height="42" rx="4" style="fill:var(--warn);fill-opacity:.05;stroke:var(--warn);stroke-width:1.2"/><text x="98.0" y="164.0" text-anchor="start" style="fill:var(--warn);font-size:12px;font-weight:700">contiguous（C++20）</text><text x="278.0" y="164.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">+ 元素在内存里连续</text><text x="602.0" y="164.0" text-anchor="end" style="fill:var(--muted);font-size:11px">vector、array、string、原始指针</text><text x="0.0" y="242.0" text-anchor="start" style="fill:var(--muted);font-size:11px">外层的要求最少，越往里能做的运算越多；能用 std::sort 的只有 random_access 以内的</text></g></svg>
</figure>

标准算法通过 `std::iterator_traits<It>::iterator_category` 在编译期做**标签分派**（tag dispatch）选择最快实现（标签之间是继承关系，没专门重载的自动落到基类标签）：

```cpp
// 《迭代器模式》：标签分派——编译器按 iterator_category 静态选择 O(1) 或 O(n) 实现
#include <cstdio>
#include <iterator>
#include <list>
#include <vector>

template <class It>
long myDistance(It a, It b, std::random_access_iterator_tag) {
    std::printf("  [random_access: O(1) 相减] "); return static_cast<long>(b - a);
}
template <class It>
long myDistance(It a, It b, std::input_iterator_tag) {
    std::printf("  [其他: O(n) 步进] ");
    long n = 0; for (; a != b; ++a) ++n; return n;
}
template <class It>
long myDistance(It a, It b) {
    return myDistance(a, b, typename std::iterator_traits<It>::iterator_category{});
}

int main() {
    std::vector<int> v(1000);
    std::list<int> l(1000);
    std::printf("%ld\n", myDistance(v.begin(), v.end()));
    std::printf("%ld\n", myDistance(l.begin(), l.end()));
}
```

```text
  [random_access: O(1) 相减] 1000
  [其他: O(n) 步进] 1000
```

### 3.2 分类标错了会怎样：实测对比

用 $2^{20}$（约 100 万）个时间戳实测同一个环形缓冲区迭代器标成不同分类，对比 `std::list`（完整压测代码见 `projects/dp-lab/22-iterator/bench_category.cpp`）：

| `-O2` 耗时（µs，7 遍中位数） | Clang `distance` | Clang `lower_bound` | GCC `distance` | GCC `lower_bound` |
| --- | ---: | ---: | ---: | ---: |
| Ring 标成 `forward` | 0.004 | 0.19 | 0.004 | 0.015 |
| Ring 标成 `random_access` | 0.004 | 0.19 | 0.000 | 0.017 |
| `std::list`（`bidirectional`） | 892 | 1503 | 0.000 | 939 |

`-O2` 下优化器把简单的 `++p_` 循环折叠成了加法，抹平了 `Ring` 标错的差距（但 `std::list` 顺着指针走，优化器救不回来）；可到了 `-O0` 调试版，标低分类会让 `lower_bound` 真正步进上百万次，**慢 1～2 万倍**：

<figure class="diagram">
<svg viewBox="0 0 730 186" role="img" aria-label="-O0 编译时，同一个 Ring 迭代器标成 forward、bidirectional、random_access 三种分类，再和 std::list 比：std::lower_bound 每次的耗时"><g style="font-family:var(--mono)"><text x="188.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang -O0（macOS）</text><rect x="188.0" y="30.0" width="132.1" height="20.0" rx="2" style="fill:var(--warn)"/><text x="326.1" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">2862 µs</text><rect x="188.0" y="60.0" width="136.1" height="20.0" rx="2" style="fill:var(--warn)"/><text x="330.1" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">2949 µs</text><rect x="188.0" y="90.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="195.5" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.29 µs</text><rect x="188.0" y="120.0" width="175.7" height="20.0" rx="2" style="fill:var(--danger)"/><text x="369.7" y="135.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">3807 µs</text><text x="458.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC -O0（Ubuntu）</text><rect x="458.0" y="30.0" width="114.5" height="20.0" rx="2" style="fill:var(--warn)"/><text x="578.5" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">2481 µs</text><rect x="458.0" y="60.0" width="115.4" height="20.0" rx="2" style="fill:var(--warn)"/><text x="579.4" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">2500 µs</text><rect x="458.0" y="90.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="465.5" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.115 µs</text><rect x="458.0" y="120.0" width="155.2" height="20.0" rx="2" style="fill:var(--danger)"/><text x="619.2" y="135.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">3362 µs</text><text x="180.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">Ring 标成 forward</text><text x="180.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">Ring 标成 bidirectional</text><text x="180.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">Ring 标成 random_access</text><text x="180.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">std::list</text><text x="10.0" y="178.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench_category：2^20 个时间戳，20 次 std::lower_bound，7 遍取中位数，单位微秒/次</text></g></svg>
</figure>

原则：**能力够就把分类如实写高，绝不能虚报**。

### 3.3 迭代器失效与环形缓冲区覆盖坑

容器结构变化会让已发出的迭代器过时：

| 容器 | 插入 | 删除 |
| --- | --- | --- |
| `vector` | 扩容时全部失效；未扩容时插入点之后失效 | 删除点及之后失效 |
| `deque` | 两端插入迭代器全失效（引用有效）；中间全失效 | 删两端仅被删失效；删中间全失效 |
| `list` / `map` / `set` | 均不失效 | 仅被删节点失效 |
| `unordered_map` | rehash 时迭代器全失效（引用有效） | 仅被删节点失效 |

`vector` 扩容后旧迭代器变成悬空指针，不崩溃却读到垃圾值，需靠 `-fsanitize=address`（报 `heap-use-after-free`）或 GCC `-D_GLIBCXX_DEBUG` 捕获（详见 `projects/dp-lab/22-iterator/invalidation.cpp`）。

而环形缓冲区不会重分配内存，ASan 抓不到它——写满覆盖旧帧后，迭代器**没悬空却悄悄指错了帧**。解决办法是在迭代器里存单调递增的帧序号 `p_`，解引用前校验 `p_ >= read_ && p_ <= write_`：

```cpp
// 《迭代器模式》：环形缓冲区覆盖旧帧属于「逻辑失效」——用单调序号 valid() 捕获
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

struct AudioFrame { std::int64_t pts; float peak; };

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
        bool valid() const { return p_ >= r_->read_ && p_ <= r_->write_; }
        const AudioFrame& operator*() const {
            assert(valid() && "迭代器指向的帧已经被覆盖");
            return r_->buf_[p_ & (r_->buf_.size() - 1)];
        }
        const AudioFrame* operator->() const { return &**this; }
        std::uint64_t seq() const { return p_; }
    private:
        const FrameRing* r_;
        std::uint64_t p_;
    };
    const_iterator begin() const { return {this, read_}; }
    const AudioFrame& rawSlot(std::uint64_t seq) const { return buf_[seq & (buf_.size() - 1)]; }
private:
    std::vector<AudioFrame> buf_;
    std::uint64_t read_ = 0, write_ = 0;
};

int main() {
    FrameRing ring(8);
    for (int k = 0; k < 8; ++k) ring.push({k * 1024LL, 0.1f});
    auto it = ring.begin();
    std::printf("初始：it 指向第 %lld 帧，valid=%d\n", static_cast<long long>(it->pts / 1024), it.valid());

    for (int k = 8; k < 11; ++k) ring.push({k * 1024LL, 0.9f});   // 覆盖最老的 3 帧
    std::printf("覆盖后：原格子已变成第 %lld 帧（原要第 %llu 帧），valid=%d\n",
                static_cast<long long>(ring.rawSlot(it.seq()).pts / 1024),
                static_cast<unsigned long long>(it.seq()), it.valid());
    std::printf("当前 begin() 是第 %lld 帧\n", static_cast<long long>(ring.begin()->pts / 1024));
}
```

```text
初始：it 指向第 0 帧，valid=1
覆盖后：原格子已变成第 8 帧（原要第 0 帧），valid=0
当前 begin() 是第 3 帧
```

### 3.4 惰性输入迭代器与工程实践

迭代器不一定指向已在内存里的容器。在 `projects/dp-lab/22-iterator/flv_packets.cpp` 中，我们对 `bbb-10s.flv`（714 个包，3 000 038 字节）对比了「一次全读进 `vector`」和「按需读 tag、复用单个缓冲区的 `input_iterator`」：

<figure class="diagram">
<svg viewBox="0 0 700 126" role="img" aria-label="遍历 FLV 文件的 714 个包：一次全读进 vector 峰值 3.1 MB、new 728 次；输入迭代器峰值 0.28 MB、new 3 次"><g style="font-family:var(--mono)"><text x="158.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">峰值内存 Clang</text><rect x="158.0" y="30.0" width="171.7" height="20.0" rx="2" style="fill:var(--warn)"/><text x="335.7" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">3148 KB</text><rect x="158.0" y="60.0" width="15.4" height="20.0" rx="2" style="fill:var(--accent)"/><text x="179.4" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">283 KB</text><text x="428.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">峰值内存 GCC</text><rect x="428.0" y="30.0" width="171.2" height="20.0" rx="2" style="fill:var(--warn)"/><text x="605.2" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">3139 KB</text><rect x="428.0" y="60.0" width="14.9" height="20.0" rx="2" style="fill:var(--accent)"/><text x="448.9" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">274 KB</text><text x="150.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">一次全读进 vector</text><text x="150.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">输入迭代器</text><text x="10.0" y="118.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">flv_packets：bbb-10s.flv（3 000 038 字节），重载 operator new 统计；不含 FILE 自己的缓冲区</text></g></svg>
</figure>

<figure class="diagram">
<svg viewBox="0 0 740 186" role="img" aria-label="拿到第一个包、找 5 秒后第一个关键帧的耗时：一次全读都要读完整个文件；输入迭代器首包快 30 多倍，找关键帧快约 3 倍"><g style="font-family:var(--mono)"><text x="198.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang（macOS）</text><rect x="198.0" y="30.0" width="74.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="278.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">0.37 ms</text><rect x="198.0" y="60.0" width="2.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="206.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.01 ms</text><rect x="198.0" y="90.0" width="74.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="278.0" y="105.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">0.37 ms</text><rect x="198.0" y="120.0" width="26.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="230.0" y="135.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.13 ms</text><text x="468.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC（Ubuntu，Docker）</text><rect x="468.0" y="30.0" width="166.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="640.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">0.83 ms</text><rect x="468.0" y="60.0" width="12.4" height="20.0" rx="2" style="fill:var(--accent)"/><text x="486.4" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.062 ms</text><rect x="468.0" y="90.0" width="162.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="636.0" y="105.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">0.81 ms</text><rect x="468.0" y="120.0" width="56.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="530.0" y="135.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.28 ms</text><text x="190.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">全读：拿到第一个包</text><text x="190.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">迭代器：拿到第一个包</text><text x="190.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">全读：找 5 秒后关键帧</text><text x="190.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">迭代器：找 5 秒后关键帧</text><text x="10.0" y="178.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">flv_packets：-O2，每项 7 遍取中位数，文件已在系统缓存里；整个程序跑 3 次取中间那次</text></g></svg>
</figure>

输入迭代器把峰值内存从 3.1 MB（`new` 728 次）压到 0.28 MB（`new` 3 次，仅等于最大单个关键帧），首包快 30 多倍，`std::find_if` 找到 5 秒后关键帧即停、只读了 1/3 文件；代价是只能单遍遍历。

真实 C 库 **FFmpeg** 也是同样思路：`av_read_frame(fmt, pkt)` 和 `av_codec_iterate(&opaque)` 用不透明状态记住进度，外部包一层 `input_iterator` 即可接入 range-for 与 `<algorithm>`（完整包装见 `projects/dp-lab/22-iterator/ffmpeg/packets.cpp`）。此外，STL 这种由调用方驱动的叫**外部迭代器**，能轻松同时推进两个序列（如封装器用 `std::merge` 按 DTS 交错合并音视频流）；由容器控制循环的 `forEach(cb)` 叫**内部迭代器**，写树遍历更省事，但无法直接双序列合并（对比代码见 `projects/dp-lab/22-iterator/internal_external.cpp`）。

## 4. 现代 C++：C++20 Ranges、Concept 与协程生成器

### 4.1 C++20 Concept 与 `std::views` 管道

C++20 把迭代器约定正式写成 concept（如 `std::forward_iterator`），`!=` 由 `==` 自动生成，通过 `static_assert` 即可在编译期校验，并无缝接入 `std::ranges` 投影与惰性 `std::views` 管道：

<!-- run: flags="-std=c++20" -->
```cpp
// 《迭代器模式》：C++20 concept 校验 + std::ranges 投影与 std::views 惰性管道
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
        using iterator_concept = std::forward_iterator_tag;
        using value_type       = AudioFrame;
        using difference_type  = std::ptrdiff_t;

        const_iterator() = default;
        const_iterator(const FrameRing* r, std::uint64_t p) : r_(r), p_(p) {}
        const AudioFrame& operator*() const { return r_->buf_[p_ & (r_->buf_.size() - 1)]; }
        const_iterator& operator++() { ++p_; return *this; }
        const_iterator operator++(int) { auto t = *this; ++p_; return t; }
        bool operator==(const const_iterator& o) const { return p_ == o.p_; }
    private:
        const FrameRing* r_ = nullptr;
        std::uint64_t p_ = 0;
    };
    const_iterator begin() const { return {this, read_}; }
    const_iterator end()   const { return {this, write_}; }
private:
    std::vector<AudioFrame> buf_;
    std::uint64_t read_ = 0, write_ = 0;
};

static_assert(std::forward_iterator<FrameRing::const_iterator>);
static_assert(!std::bidirectional_iterator<FrameRing::const_iterator>);
static_assert(std::ranges::forward_range<FrameRing>);

int main() {
    FrameRing ring(8);
    for (int k = 0; k < 11; ++k) ring.push({k * 1024LL, 1024, 0.1f * (k % 7)});

    auto loudest = std::ranges::max(ring, {}, &AudioFrame::peak);  // 投影：免写 lambda
    std::printf("最响的一帧：第 %lld 帧，峰值 %.1f\n",
                static_cast<long long>(loudest.pts / 1024), loudest.peak);

    auto loud = ring | std::views::filter([](const AudioFrame& f) { return f.peak > 0.2f; })
                     | std::views::transform([](const AudioFrame& f) { return f.pts / 1024; })
                     | std::views::take(3);                        // 惰性管道：取够 3 个就停
    std::printf("峰值超过 0.2 的前 3 帧：");
    for (long long k : loud) std::printf("%lld ", k);
    std::printf("\n");
}
```

```text
最响的一帧：第 6 帧，峰值 0.6
峰值超过 0.2 的前 3 帧：3 4 5 
```

用 concept 约束后，误对 `std::forward_list` 调 `std::ranges::sort` 时，编译器会直接报 `does not satisfy random_access_range`（25～31 行），告别 C++17 上百行的模板内部天书报错。

### 4.2 协程生成器简述与选型

写 `FlvReader` 这类状态机迭代器通常要拆成多个成员变量；C++20 协程（及 C++23 `std::generator`）允许把解析写成普通 `while` 循环，每解析出一个包就 `co_yield` 暂停，状态自动保存在协程帧里（完整示例见 `projects/dp-lab/22-iterator/generator.cpp`）。代价是每次 `++` 涉及协程恢复开销且仅支持单遍 `input` 遍历，适合**带 I/O 或复杂解析逻辑**的惰性流，不适合环形缓冲区这种极轻量的热循环。

| 场景 | 推荐写法 |
| --- | --- |
| 内部就是标准容器 | 直接转发 `begin()`/`end()`，别手写迭代器类 |
| 自研数据结构（环形缓冲、对象池、树） | 手写 STL 迭代器（5 个别名 + 运算），能力够就标高分类，C++20 加 `static_assert` |
| 边读边处理（文件、网络、解码） | `input_iterator` 或 C++20/23 协程生成器 |
| 对已有序列过滤、变换、截取 | C++20 `std::views` 惰性管道，零中间容器 |
| 跨 C ABI 隐藏内部结构 | FFmpeg 风格 `xxx_iterate(&opaque)` |

## 5. 迭代器和相似的模式

| 模式 | 核心关注点 | 和迭代器的关系 |
| --- | --- | --- |
| **组合** | 把对象组织成树形结构 | 组合定义**结构**，迭代器定义**走法**（显式栈可把树封装成前序迭代器） |
| **访问者** | 对不同类型元素新增操作而不改元素类 | 迭代器管**顺序**，访问者管对每个元素**做什么**，常配合使用 |
| **工厂方法** | 由子类决定创建哪种对象 | GoF 写法的 `createIterator()` 本身就是一个工厂方法 |
| **观察者** | 数据变化时主动推送给订阅者 | 观察者是**推（push）**，外部迭代器是**拉（pull）** |

## 6. 小结

- 迭代器把「如何遍历」封装进游标，调用方不碰内部下标：内部结构重构成「2 的幂 + 计数器」时，调用方 0 处改动。
- C++ 用 `begin/end`、`*`、`++`、`==` 和五个类型别名代替 GoF 虚接口，直接对接 range-for 与 `<algorithm>`；C++20 用 concept 检查并支持 `ranges/views` 惰性管道。
- 分类决定算法复杂度：标低分类在 `-O0` 下 `lower_bound` 慢 1～2 万倍，标高分类则是错误。
- 物理失效（`vector` 扩容悬空）靠 ASan / `_GLIBCXX_DEBUG` 抓；环形缓冲区覆盖属于逻辑失效，需靠单调序号校验。
- 惰性输入迭代器适合大文件与流媒体（FLV 峰值内存降 10 倍、首包快 30 多倍），代价是只能单遍遍历。

## 7. 练习

1. **辨析**：下面四个迭代器各属于哪一类（input / forward / bidirectional / random_access / contiguous）？
   - (a) 第 2.2 节的 `FrameRing::const_iterator`（按现在声明的，以及它**最高**能做到哪一类）；
   - (b) 第 3.4 节的 `FlvReader::iterator` 和 `std::filesystem::directory_iterator`；
   - (c) `std::map<std::int64_t, Packet>::iterator` 与 `std::unordered_map<int, Stream>::iterator`；
   - (d) `std::string::iterator`（C++20）。
2. **设计**：给《组合模式》里的文件树设计一个不用递归的前序遍历范围 `PreorderRange`，支持 range-for 和 `std::count_if`，并能查询当前节点的缩进深度 `depth()`。迭代器内部应该保存什么状态？`operator++()` 怎么推进？
3. **找问题**：播放时钟走到 60 ms 时，要把比时钟早 10 ms 以上的过期包丢掉。下面代码漏删了 `A10`，问题出在哪？怎么改？

```cpp
// 《迭代器模式》练习 3：边遍历 vector 边 erase，导致过期包 A10 被跳过
#include <cstdint>
#include <cstdio>
#include <vector>

struct Packet { char stream; std::int64_t dts; };

int main() {
    std::vector<Packet> q = {{'V', 0}, {'A', 10}, {'A', 31}, {'V', 40}, {'A', 52}, {'V', 80}, {'A', 94}};
    const std::int64_t clock = 60;
    for (auto it = q.begin(); it != q.end(); ++it)
        if (it->dts < clock - 10) q.erase(it);   // 失效！
    std::printf("剩下：");
    for (const auto& p : q) std::printf("%c%lld ", p.stream, static_cast<long long>(p.dts));
    std::printf("\n");
}
```

<details>
<summary>点击查看答案</summary>

1. (a) 当前声明为 **forward**，最高可做成 **random_access**（第 `n` 个元素 O(1) 算得，但绕回内存不连续，不能是 contiguous）；
   (b) 均为 **input**（每次 `++` 覆盖缓冲区或触发 `readdir` 系统调用，只能单遍）；
   (c) `std::map` 是 **bidirectional**（红黑树），`std::unordered_map` 是 **forward**（单链表桶）；
   (d) **contiguous**（C++20 保证字符内存连续）。

2. 迭代器内部维护一个显式栈 `std::vector<Frame>`，每帧记录 `{const Node* node, std::size_t nextChild}`，`depth()` 即 `stack_.size() - 1`。`operator++()` 时：若栈顶节点有子节点，把第一个子节点压栈；否则不断弹栈直到找到还有下一个兄弟的父节点，把下一个兄弟压栈；栈空即等于 `end()`。完整可运行代码见 `projects/dp-lab/22-iterator/ex3_tree_iter.cpp`（双向环形缓冲区练习见 `ex2_bidirectional.cpp`）。

3. `q.erase(it)` 会使 `it` 及之后的迭代器失效：删除 `V0` 后 `A10` 被前移到原位置，循环末尾的 `++it` 直接跳过了 `A10`（删最后一个元素还会越界 UB）。两种改法：
   - 接收 `erase` 返回值：`if (it->dts < clock - 10) it = q.erase(it); else ++it;`（最坏 $O(n^2)$）；
   - 推荐 **erase-remove 惯用法**：`q.erase(std::remove_if(q.begin(), q.end(), ...), q.end());`（C++20 直接 `std::erase_if(q, pred)`），一遍搬完 $O(n)$。完整版见 `projects/dp-lab/22-iterator/ex4_fixed.cpp`。

</details>
