---
title: 享元模式
date: 2026-11-12 10:00:00
updated: 2026-11-12 10:00:00
categories:
  - 设计模式
  - 结构型模式
tags:
  - 设计模式
  - 享元
  - 字幕
  - 字符串驻留
  - 内存
order: 6
description: 播放器把一条 90 分钟的中文字幕预先排版好，每个字一个对象、各带一张 48×48 的字形位图：19440 个字占了 44.4 MB，光栅化 19440 次，可这些字只有 2315 种，88% 的位图是重复的。享元模式把对象拆成能共享的内部状态（字体、字号、字、位图）和每次不同的外部状态（位置、颜色、时间），由工厂缓存享元。实测内存降到 5.6 MB，排版从 57.4 ms 降到 6.9 ms；GCC 上不共享的版本还要多分配一倍的内存块（字体名超出了短字符串优化）。再讲享元为什么必须不可变、字符串驻留、shared_ptr 和 weak_ptr 缓存、多线程下工厂加锁的代价（4 线程 mutex 每次查找 43.7 ns，是单线程的近 20 倍），以及享元和单例、原型、对象池的区别。
top_group_list: true
disableNunjucks: true
viz: [dp-flyweight]
---

> **本文目标**：理解享元模式解决的问题：大量相似的小对象里有大量重复的数据；会把对象的状态分成内部状态和外部状态，写一个缓存享元的工厂；知道享元必须不可变、工厂的存储不能让享元搬家、多线程下工厂要加锁；会用字符串驻留、`string_view`、`shared_ptr`/`weak_ptr` 这些 C++ 里常见的享元写法；分清享元和单例、原型、对象池。
>
> **前置知识**：上一篇《外观模式》；《单例模式》《原型模式》（第 5 节要和它们比较）；C/C++ 专栏的《智能指针：unique_ptr、shared_ptr 与 weak_ptr》；知道 `std::unordered_map` 的基本用法。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。完整代码在仓库的 `projects/dp-lab/16-flyweight/` 目录。

## 1. 问题：一条字幕，44 MB

### 1.1 每个字一个对象

播放器显示字幕，要先把每个字的轮廓画成一张灰度位图（光栅化，FreeType 做的就是这件事），再按位置和颜色叠到视频画面上。有些场景会把整条字幕预先排版好：字幕编辑器的时间轴要随时显示任意一行，卡拉 OK 字幕要逐字变色，拖动进度条时不能等字幕现排。

最直接的写法是每个字一个 `Glyph` 对象，里面有这个字的全部信息：哪个字体、多大、哪个字、位图，以及它在屏幕上的位置、颜色、什么时候显示。下面用一条模拟的 90 分钟中文字幕来测：1400 行，每行 8～20 个字，从 3000 个常用字里按 Zipf 分布抽取（越常用的字出现得越多，真实文本大致是这样）。光栅化也是模拟的，每个字生成一张 48×48 的 8 位位图：

```cpp
// 《享元模式》：不共享——整条字幕轨排版后，每个字符都是一个完整的 Glyph 对象，各自带一份字形位图
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <unordered_set>
#include <vector>

// ---- 统计堆分配：替换全局 operator new / delete，在每块内存前面记下大小 ----
static std::size_t g_allocs = 0, g_live = 0;
void* operator new(std::size_t n) {
    ++g_allocs;
    g_live += n;
    auto* h = static_cast<std::size_t*>(std::malloc(n + 16));
    if (!h) std::abort();
    *h = n;
    return reinterpret_cast<char*>(h) + 16;
}
void operator delete(void* p) noexcept {
    if (!p) return;
    auto* h = reinterpret_cast<std::size_t*>(static_cast<char*>(p) - 16);
    g_live -= *h;
    std::free(h);
}
void operator delete(void* p, std::size_t) noexcept { operator delete(p); }

// ---- 一条 90 分钟电影的中文字幕：1400 行，每行 8～20 个字，用字频率按 Zipf 分布 ----
std::vector<std::u32string> make_track(int lines) {
    const int kPool = 3000;  // 3000 个常用字，第 k 常用的字出现的频率 ∝ 1/k
    std::vector<double> cdf(kPool);
    double sum = 0;
    for (int k = 0; k < kPool; ++k) cdf[k] = sum += 1.0 / (k + 1);
    std::uint32_t s = 2026;
    auto rnd = [&s] { s = s * 1664525u + 1013904223u; return s >> 8; };
    std::vector<std::u32string> track;
    for (int i = 0; i < lines; ++i) {
        std::u32string text;
        int len = 8 + rnd() % 13;
        for (int j = 0; j < len; ++j) {
            double u = (rnd() % 1000000) / 1e6 * sum;
            text += char32_t(0x4E00 + (std::lower_bound(cdf.begin(), cdf.end(), u) - cdf.begin()));
        }
        track.push_back(text);
    }
    return track;
}

// 模拟 FreeType 把一个字的轮廓画成 px×px 的 8 位灰度位图
static int g_raster = 0;
std::vector<std::uint8_t> rasterize(char32_t ch, int px) {
    ++g_raster;
    std::vector<std::uint8_t> bm(px * px);
    std::uint32_t h = ch * 2654435761u;
    for (auto& b : bm) { h ^= h << 13; h ^= h >> 17; h ^= h << 5; b = std::uint8_t(h); }
    return bm;
}

struct Glyph {
    // 内部状态：只取决于「哪个字体、多大、哪个字」
    std::string font;
    int px;
    char32_t ch;
    int advance;
    std::vector<std::uint8_t> bitmap;
    // 外部状态：每个字符在屏幕上的位置、颜色、显示时间
    float x, y;
    std::uint32_t color;
    int start_ms, end_ms;
};

int main() {
    auto track = make_track(1400);
    std::size_t chars = 0;
    std::unordered_set<char32_t> distinct;
    for (auto& t : track) { chars += t.size(); distinct.insert(t.begin(), t.end()); }

    std::size_t a0 = g_allocs, m0 = g_live;
    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::vector<Glyph>> lines;  // 排版结果：每行一组 Glyph
    lines.reserve(track.size());
    const int px = 48;
    for (std::size_t i = 0; i < track.size(); ++i) {
        std::vector<Glyph> line;
        line.reserve(track[i].size());
        float x = 960 - px * track[i].size() / 2.0f;
        for (char32_t ch : track[i]) {
            line.push_back(Glyph{"Noto Sans CJK SC", px, ch, px, rasterize(ch, px),
                                 x, 1000, 0xFFFFFFFF, int(i) * 3500, int(i) * 3500 + 3000});
            x += px;
        }
        lines.push_back(std::move(line));
    }
    auto t1 = std::chrono::steady_clock::now();

    long long ck = 0;  // 「画」一遍：每个字取位图中间的一个像素，加上位置
    for (auto& line : lines)
        for (auto& g : line) ck += g.bitmap[px * px / 2] + int(g.x);

    std::printf("字幕：%zu 行，%zu 个字，%zu 种不同的字\n", track.size(), chars, distinct.size());
    std::printf("Glyph 对象：%zu 个，sizeof(Glyph) = %zu 字节\n", chars, sizeof(Glyph));
    std::printf("光栅化：%d 次\n", g_raster);
    std::printf("堆分配：%zu 次，占用 %.1f MB\n", g_allocs - a0, (g_live - m0) / 1048576.0);
    std::printf("排版用时：%.0f ms\n", std::chrono::duration<double, std::milli>(t1 - t0).count());
    std::printf("校验和：%lld\n", ck);
}
```

```
字幕：1400 行，19440 个字，2315 种不同的字
Glyph 对象：19440 个，sizeof(Glyph) = 88 字节
光栅化：19440 次
堆分配：20841 次，占用 44.4 MB
排版用时：83 ms
校验和：20573269
```

程序开头替换了全局的 `operator new` / `operator delete`，在每块内存前面记下它的大小，这样就能数出排版前后多分配了多少次、多占了多少字节。（在浏览器里运行时数字会小一些：WASM 是 32 位的，指针和 `size_t` 只有 4 字节。）

### 1.2 重复在哪里

19440 个字只有 **2315 种**。「的」「是」「我」这些字出现了几百次，每次都光栅化一遍，存一张一模一样的 2304 字节的位图：

- **内存**：位图一共 19440 × 2304 ≈ 42.7 MB，其中只有 2315 张是不同的，**88% 是重复的**。再加上每个对象 88 字节和字体名，总共 44.4 MB。这只是一条字幕；双语字幕、更大的字号（4K 视频上 48 像素的字太小了）、描边和阴影各一张位图，都会成倍增加。
- **时间**：光栅化了 19440 次。这里的模拟光栅化每个字约 3 微秒，真正的 FreeType 要处理贝塞尔曲线、抗锯齿、hinting，一个中文字通常要几十微秒，重复的工作就更贵了。
- **在 GCC 上还多一倍的内存分配**：字体名 `"Noto Sans CJK SC"` 有 16 个字节。libc++ 的 `std::string` 能把 22 个字节以内的字符串直接存在对象里（短字符串优化，SSO），libstdc++ 只能存 15 个字节，于是 GCC 编译的版本每个字还要为字体名单独分配一次：堆分配从 20841 次变成 40281 次。

问题在于，**一个 `Glyph` 里混着两种性质完全不同的数据**：

- 字体、字号、字、步进宽度、位图：只取决于「这是哪个字」，所有的「我」都一样；
- 位置、颜色、显示时间：每一个「我」都不一样。

前一种完全可以只存一份。

## 2. 享元模式

### 2.1 意图

GoF 的定义：

> 运用共享技术有效地支持大量细粒度的对象。（Use sharing to support large numbers of fine-grained objects efficiently.）

大白话：**对象很多、彼此很像的时候，把它们相同的部分抽出来只存一份，大家共用。**

享元模式把对象的状态分成两部分：

- **内部状态（intrinsic state）**：只取决于享元自己是什么，与在哪里使用无关，因此可以共享。存在享元对象里，创建以后不再改变。
- **外部状态（extrinsic state）**：随使用的场合而变，不能共享。由使用者保存，用到享元的时候再传给它（或者和指向享元的指针放在一起）。

<figure class="diagram">
<svg viewBox="0 0 600 270" role="img" aria-label="一个 FatGlyph 有 8 个字段：字体、字号、字、步进、位图是内部状态，位置、颜色、显示时间是外部状态。拆开后，内部状态成为享元 Glyph，2315 个，由工厂持有；外部状态成为 16 字节的 PlacedGlyph，19440 个，各有一个指针指向享元"><g style="font-family:var(--mono)"><text x="10.0" y="20.0" text-anchor="start" style="fill:var(--fg);font-size:12.5px;font-weight:700">不共享：每个字一个 FatGlyph</text><rect x="10" y="34" width="230" height="188" rx="4" style="fill:none;stroke:var(--border);stroke-width:1.2"/><text x="22.0" y="56.0" text-anchor="start" style="fill:var(--warn);font-size:12px">font = "Noto Sans CJK SC"</text><text x="22.0" y="78.0" text-anchor="start" style="fill:var(--warn);font-size:12px">px = 48</text><text x="22.0" y="100.0" text-anchor="start" style="fill:var(--warn);font-size:12px">ch = U'我'</text><text x="22.0" y="122.0" text-anchor="start" style="fill:var(--warn);font-size:12px">advance = 48</text><text x="22.0" y="144.0" text-anchor="start" style="fill:var(--warn);font-size:12px">bitmap: 2304 字节</text><text x="22.0" y="166.0" text-anchor="start" style="fill:var(--link);font-size:12px">x, y</text><text x="22.0" y="188.0" text-anchor="start" style="fill:var(--link);font-size:12px">color</text><text x="22.0" y="210.0" text-anchor="start" style="fill:var(--link);font-size:12px">start_ms, end_ms</text><text x="10.0" y="246.0" text-anchor="start" style="fill:var(--warn);font-size:12px;font-weight:700">× 19440 个，44.4 MB</text><line x1="260" y1="120" x2="310" y2="120" style="stroke:var(--muted);stroke-width:1.5"/><path d="M316,120 L308,115 L308,125 z" style="fill:var(--muted)"/><text x="288.0" y="110.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">拆开</text><text x="330.0" y="20.0" text-anchor="start" style="fill:var(--link);font-size:12.5px;font-weight:700">外部状态：PlacedGlyph（16 字节）</text><rect x="330" y="34" width="200" height="56" rx="4" style="fill:none;stroke:var(--link);stroke-width:1.2"/><text x="342.0" y="56.0" text-anchor="start" style="fill:var(--fg);font-size:12px">glyph: const Glyph*</text><text x="342.0" y="78.0" text-anchor="start" style="fill:var(--link);font-size:12px">x, y</text><text x="330.0" y="110.0" text-anchor="start" style="fill:var(--link);font-size:11.5px">× 19440 个；color、时间放到 Line 上</text><text x="330.0" y="142.0" text-anchor="start" style="fill:var(--warn);font-size:12.5px;font-weight:700">内部状态：Glyph（享元，不可变）</text><rect x="330" y="150" width="200" height="78" rx="4" style="fill:none;stroke:var(--warn);stroke-width:1.2"/><text x="342.0" y="172.0" text-anchor="start" style="fill:var(--warn);font-size:12px">font, px, ch</text><text x="342.0" y="194.0" text-anchor="start" style="fill:var(--warn);font-size:12px">advance</text><text x="342.0" y="216.0" text-anchor="start" style="fill:var(--warn);font-size:12px">bitmap: 2304 字节</text><text x="330.0" y="248.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px">× 2315 个，由 GlyphFactory 持有</text><path d="M520,52 C590,64 590,170 532,180" style="fill:none;stroke:var(--muted);stroke-width:1.3;stroke-dasharray:4 3"/><path d="M532,180 L542,173 L542,183 z" style="fill:var(--muted)"/></g></svg>
</figure>

「享元」是 flyweight 的意译，这个英文词原本是拳击里最轻的「蝇量级」：每个使用者手里只剩一个指针和一点外部状态，很轻。GoF 书里的例子正是文字编辑器：文档里的每个字符都是一个对象，但同一个字符只有一个对象，它的位置由所在的行和列决定。

### 2.2 用享元重构

`Glyph` 只保留内部状态，由 `GlyphFactory` 按「字体、字号、字」缓存；排版结果里每个字只剩一个 `PlacedGlyph`：一个指向享元的指针和一个位置。颜色和显示时间是整行共用的，干脆放到 `Line` 上：

```cpp
// 《享元模式》：GlyphFactory 让相同的字共享同一个 Glyph，每个字符只剩一个指针和位置
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ---- 统计堆分配：替换全局 operator new / delete，在每块内存前面记下大小 ----
static std::size_t g_allocs = 0, g_live = 0;
void* operator new(std::size_t n) {
    ++g_allocs;
    g_live += n;
    auto* h = static_cast<std::size_t*>(std::malloc(n + 16));
    if (!h) std::abort();
    *h = n;
    return reinterpret_cast<char*>(h) + 16;
}
void operator delete(void* p) noexcept {
    if (!p) return;
    auto* h = reinterpret_cast<std::size_t*>(static_cast<char*>(p) - 16);
    g_live -= *h;
    std::free(h);
}
void operator delete(void* p, std::size_t) noexcept { operator delete(p); }

// ---- 一条 90 分钟电影的中文字幕：1400 行，每行 8～20 个字，用字频率按 Zipf 分布 ----
std::vector<std::u32string> make_track(int lines) {
    const int kPool = 3000;  // 3000 个常用字，第 k 常用的字出现的频率 ∝ 1/k
    std::vector<double> cdf(kPool);
    double sum = 0;
    for (int k = 0; k < kPool; ++k) cdf[k] = sum += 1.0 / (k + 1);
    std::uint32_t s = 2026;
    auto rnd = [&s] { s = s * 1664525u + 1013904223u; return s >> 8; };
    std::vector<std::u32string> track;
    for (int i = 0; i < lines; ++i) {
        std::u32string text;
        int len = 8 + rnd() % 13;
        for (int j = 0; j < len; ++j) {
            double u = (rnd() % 1000000) / 1e6 * sum;
            text += char32_t(0x4E00 + (std::lower_bound(cdf.begin(), cdf.end(), u) - cdf.begin()));
        }
        track.push_back(text);
    }
    return track;
}

// 模拟 FreeType 把一个字的轮廓画成 px×px 的 8 位灰度位图
static int g_raster = 0;
std::vector<std::uint8_t> rasterize(char32_t ch, int px) {
    ++g_raster;
    std::vector<std::uint8_t> bm(px * px);
    std::uint32_t h = ch * 2654435761u;
    for (auto& b : bm) { h ^= h << 13; h ^= h >> 17; h ^= h << 5; b = std::uint8_t(h); }
    return bm;
}

// 享元：只保存内部状态，创建以后不再修改
struct Glyph {
    int font;  // 字体编号（字体名只存一份，见 fonts）
    int px;
    char32_t ch;
    int advance;
    std::vector<std::uint8_t> bitmap;
};

struct GlyphKey {
    int font, px;
    char32_t ch;
    bool operator==(const GlyphKey& o) const { return font == o.font && px == o.px && ch == o.ch; }
};
struct GlyphKeyHash {
    std::size_t operator()(const GlyphKey& k) const {
        return (std::size_t(k.font) * 131 + k.px) * 1000003 ^ k.ch;
    }
};

// 享元工厂：同一个 (字体, 字号, 字) 只光栅化一次，以后都返回同一个 Glyph
class GlyphFactory {
public:
    const Glyph& get(int font, int px, char32_t ch) {
        GlyphKey key{font, px, ch};
        auto it = cache_.find(key);
        if (it == cache_.end())
            it = cache_.emplace(key, Glyph{font, px, ch, px, rasterize(ch, px)}).first;
        return it->second;  // unordered_map 的元素在节点里，rehash 也不会移动它
    }
    std::size_t size() const { return cache_.size(); }

private:
    std::unordered_map<GlyphKey, Glyph, GlyphKeyHash> cache_;
};

// 外部状态：每个字只剩一个指针和位置；颜色和显示时间是整行共用的，放到行上
struct PlacedGlyph {
    const Glyph* glyph;
    float x, y;
};
struct Line {
    int start_ms, end_ms;
    std::uint32_t color;
    std::vector<PlacedGlyph> glyphs;
};

int main() {
    auto track = make_track(1400);
    std::size_t chars = 0;
    std::unordered_set<char32_t> distinct;
    for (auto& t : track) { chars += t.size(); distinct.insert(t.begin(), t.end()); }

    std::size_t a0 = g_allocs, m0 = g_live;
    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::string> fonts{"Noto Sans CJK SC"};
    GlyphFactory factory;
    std::vector<Line> lines;
    lines.reserve(track.size());
    const int px = 48;
    for (std::size_t i = 0; i < track.size(); ++i) {
        Line line{int(i) * 3500, int(i) * 3500 + 3000, 0xFFFFFFFF, {}};
        line.glyphs.reserve(track[i].size());
        float x = 960 - px * track[i].size() / 2.0f;
        for (char32_t ch : track[i]) {
            line.glyphs.push_back({&factory.get(0, px, ch), x, 1000});
            x += px;
        }
        lines.push_back(std::move(line));
    }
    auto t1 = std::chrono::steady_clock::now();

    long long ck = 0;
    for (auto& line : lines)
        for (auto& g : line.glyphs) ck += g.glyph->bitmap[px * px / 2] + int(g.x);

    std::printf("字幕：%zu 行，%zu 个字，%zu 种不同的字\n", track.size(), chars, distinct.size());
    std::printf("Glyph 对象：%zu 个，sizeof(Glyph) = %zu 字节；PlacedGlyph：%zu 个，每个 %zu 字节\n",
                factory.size(), sizeof(Glyph), chars, sizeof(PlacedGlyph));
    std::printf("光栅化：%d 次\n", g_raster);
    std::printf("堆分配：%zu 次，占用 %.1f MB\n", g_allocs - a0, (g_live - m0) / 1048576.0);
    std::printf("排版用时：%.0f ms\n", std::chrono::duration<double, std::milli>(t1 - t0).count());
    std::printf("校验和：%lld\n", ck);
}
```

```
字幕：1400 行，19440 个字，2315 种不同的字
Glyph 对象：2315 个，sizeof(Glyph) = 40 字节；PlacedGlyph：19440 个，每个 16 字节
光栅化：2315 次
堆分配：6043 次，占用 5.6 MB
排版用时：8 ms
校验和：20573269
```

校验和和不共享的版本相同：画出来的东西完全一样。几个要点：

- **工厂的键要包含全部内部状态**。同一个字，换了字体或字号就是另一张位图，所以键是 `(font, px, ch)`。字体名不放进键里，而是先换成编号：比较和哈希一个 `int` 比一个字符串便宜，键也不用分配内存。
- **享元一旦创建就不再修改**。`get()` 返回 `const Glyph&`，使用者只能读。第 3.1 节会看到改了享元会怎样。
- **享元不能搬家**。使用者保存的是享元的地址，所以工厂的存储必须保证元素的地址不变。`std::unordered_map` 的每个元素在自己的节点里，插入、rehash 都只是重新挂链表，元素本身不移动；换成 `std::vector` 就会出错（练习 4）。
- **外部状态可以再往上提**。不是每个外部状态都要跟着每个字：一行字幕的颜色、显示时间对这一行的所有字都一样，放在 `Line` 上就只存一份。这样每个字只剩 16 字节。

### 2.3 类图和时序图

```uml
label: 享元模式：字幕排版向 GlyphFactory 要 Glyph；GlyphFactory 持有所有 Glyph 享元；排版结果 Line 包含很多 PlacedGlyph，每个 PlacedGlyph 指向一个共享的 Glyph，并保存自己的位置
classes:
  - { id: Client, name: 字幕排版, x: 20, y: 46, w: 110 }
  - { id: GlyphFactory, stereo: FlyweightFactory, x: 240, y: 20, w: 270, attrs: ['- cache_ : unordered_map<GlyphKey, Glyph>'], ops: ['+ get(font, px, ch) : const Glyph&'] }
  - { id: Glyph, stereo: Flyweight, x: 630, y: 20, w: 190, attrs: ['+ font, px, ch : 内部状态', '+ advance : int', '+ bitmap : vector<uint8_t>'] }
  - { id: Line, x: 240, y: 190, w: 270, attrs: ['+ start_ms, end_ms, color', '+ glyphs : vector<PlacedGlyph>'] }
  - { id: PlacedGlyph, stereo: 外部状态, x: 630, y: 190, w: 190, attrs: ['+ glyph : const Glyph*', '+ x, y : float'] }
rels:
  - { from: Client, to: GlyphFactory, type: assoc, label: get }
  - { from: GlyphFactory, to: Glyph, type: compose, fm: '1', tm: '*' }
  - { from: Client, to: Line, type: depend, label: 生成, points: [[75, 82], [75, 225], [240, 225]] }
  - { from: Line, to: PlacedGlyph, type: compose, tm: '*' }
  - { from: PlacedGlyph, to: Glyph, type: assoc, label: 共享 }
```

GoF 的类图里还有一个 `UnsharedConcreteFlyweight`（不共享的享元）：和享元实现同一个接口，但不放进工厂。比如某一行字幕用了一个只出现一次的特效字，可以直接创建，不必进缓存。C++ 里享元很少带虚函数接口，这个角色一般不单独出现。

两个「我」的 `get()`：

```uml
label: 时序图：第一个「我」，工厂在缓存里没找到，光栅化后放进缓存再返回；第二个「我」，工厂在缓存里找到，直接返回同一个 Glyph 的地址
participants:
  - { id: l, name: 字幕排版 }
  - { id: f, name: GlyphFactory }
  - { id: c, name: cache_ }
  - { id: r, name: rasterize }
gap: 70
messages:
  - { from: l, to: f, text: "get(0, 48, '我')" }
  - { from: f, to: c, text: 'find(key)' }
  - { from: c, to: f, text: 'end()', type: reply }
  - { from: f, to: r, text: "rasterize('我', 48)", note: '约 3 μs' }
  - { from: r, to: f, text: '2304 字节的位图', type: reply }
  - { from: f, to: c, text: 'emplace(key, Glyph)' }
  - { from: f, to: l, text: '&glyph', type: reply }
  - { from: l, to: f, text: "get(0, 48, '我')" }
  - { from: f, to: c, text: 'find(key)', note: '命中，约 1～3 ns' }
  - { from: f, to: l, text: '同一个 &glyph', type: reply }
frags:
  - { from: 0, to: 6, label: '第 1 个「我」：没命中' }
  - { from: 7, to: 9, label: '第 5 个「我」：命中' }
```

### 2.4 实测：省了多少

内存和分配次数（`naive.cpp` / `flyweight.cpp`）：

<figure class="diagram">
<svg viewBox="0 0 700 126" role="img" aria-label="整条字幕排版后的堆内存：不共享 Clang 44.4 MB、GCC 44.8 MB；享元两边都是 5.6 MB"><g style="font-family:var(--mono)"><text x="158.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang / macOS</text><rect x="158.0" y="30.0" width="159.8" height="20.0" rx="2" style="fill:var(--warn)"/><text x="323.8" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">44.4 MB</text><rect x="158.0" y="60.0" width="20.2" height="20.0" rx="2" style="fill:var(--accent)"/><text x="184.2" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">5.6 MB</text><text x="428.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC 13 / Ubuntu</text><rect x="428.0" y="30.0" width="161.3" height="20.0" rx="2" style="fill:var(--warn)"/><text x="595.3" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">44.8 MB</text><rect x="428.0" y="60.0" width="20.2" height="20.0" rx="2" style="fill:var(--accent)"/><text x="454.2" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">5.6 MB</text><text x="150.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">不共享（naive）</text><text x="150.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">享元（flyweight）</text><text x="10.0" y="118.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">naive.cpp / flyweight.cpp：替换全局 operator new，统计排版前后的差值</text></g></svg>
</figure>

<figure class="diagram">
<svg viewBox="0 0 700 126" role="img" aria-label="堆分配次数：不共享 Clang 20841 次、GCC 40281 次；享元 Clang 6043 次、GCC 6042 次"><g style="font-family:var(--mono)"><text x="158.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang / macOS（libc++）</text><rect x="158.0" y="30.0" width="83.4" height="20.0" rx="2" style="fill:var(--warn)"/><text x="247.4" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">20841</text><rect x="158.0" y="60.0" width="24.2" height="20.0" rx="2" style="fill:var(--accent)"/><text x="188.2" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">6043</text><text x="428.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC 13 / Ubuntu（libstdc++）</text><rect x="428.0" y="30.0" width="161.1" height="20.0" rx="2" style="fill:var(--warn)"/><text x="595.1" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">40281</text><rect x="428.0" y="60.0" width="24.2" height="20.0" rx="2" style="fill:var(--accent)"/><text x="458.2" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">6042</text><text x="150.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">不共享（naive）</text><text x="150.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">享元（flyweight）</text><text x="10.0" y="118.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">GCC 多出约 19440 次：16 字节的字体名超出了 libstdc++ 的 SSO 上限</text></g></svg>
</figure>

内存从 44.4 MB 降到 5.6 MB，约为原来的 1/8。剩下的 5.6 MB 里，2315 张位图占 5.1 MB，19440 个 `PlacedGlyph` 只占 0.3 MB。GCC 上不共享的版本分配了 40281 次，比 Clang 多了将近一倍，原因就是 1.2 节说的短字符串优化；享元的版本字体名只存一份，两边的分配次数就几乎一样了。

耗时（`bench.cpp`，-O2，7 次取中位数）：

<figure class="diagram">
<svg viewBox="0 0 660 186" role="img" aria-label="耗时：排版整条字幕，不共享 Clang 57.4 ms、GCC 60.2 ms，享元 6.9 ms、7.4 ms；整轨绘制，不共享 2.4 ms、8.4 ms，享元 1.9 ms、8.6 ms"><g style="font-family:var(--mono)"><text x="118.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang / macOS</text><rect x="118.0" y="30.0" width="147.6" height="20.0" rx="2" style="fill:var(--warn)"/><text x="271.6" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">57.4 ms</text><rect x="118.0" y="60.0" width="17.7" height="20.0" rx="2" style="fill:var(--accent)"/><text x="141.7" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">6.9 ms</text><rect x="118.0" y="90.0" width="6.2" height="20.0" rx="2" style="fill:var(--warn)"/><text x="130.2" y="105.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">2.4 ms</text><rect x="118.0" y="120.0" width="4.9" height="20.0" rx="2" style="fill:var(--accent)"/><text x="128.9" y="135.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">1.9 ms</text><text x="388.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC 13 / Ubuntu</text><rect x="388.0" y="30.0" width="154.8" height="20.0" rx="2" style="fill:var(--warn)"/><text x="548.8" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">60.2 ms</text><rect x="388.0" y="60.0" width="19.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="413.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">7.4 ms</text><rect x="388.0" y="90.0" width="21.6" height="20.0" rx="2" style="fill:var(--warn)"/><text x="415.6" y="105.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">8.4 ms</text><rect x="388.0" y="120.0" width="22.1" height="20.0" rx="2" style="fill:var(--accent)"/><text x="416.1" y="135.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">8.6 ms</text><text x="110.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">排版·不共享</text><text x="110.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">排版·享元</text><text x="110.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">绘制·不共享</text><text x="110.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">绘制·享元</text><text x="10.0" y="178.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench.cpp：-O2，7 次取中位数；绘制读每个字的全部像素</text></g></svg>
</figure>

- **排版快了约 8 倍**，因为光栅化从 19440 次变成了 2315 次。工厂命中一次 `get()` 只要 1.2 ns（Clang）/ 3.0 ns（GCC），光栅化一个字要 2930 ns / 3038 ns，差了三个数量级：查表的开销完全可以忽略。
- **绘制没有明显变化**。整轨绘制要读每个字的全部像素，两种写法读的字节数一样多。享元的版本反复读同 2315 张位图，按理更容易命中缓存，但在 macOS 上只快了约 20%，在 GCC 的虚拟机上两者差不多（8.4 ms 对 8.6 ms）。享元主要省的是内存和创建的开销，不要指望它让每一次使用都变快。

### 2.5 演示

点「不共享」「享元」对比两种写法；点「改了享元」看把颜色（外部状态）放进享元会发生什么：

<div class="dsviz" data-viz="dp-flyweight"></div>

## 3. 用好享元

### 3.1 享元必须不可变

享元被很多使用者共享，任何一个使用者改了它，所有人都会看到。卡拉 OK 字幕要把唱到的字染成黄色，如果颜色放在享元里：

```cpp
// 《享元模式》：把外部状态（颜色）放进了享元——卡拉 OK 高亮一个字，所有同样的字都变了色
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

struct Glyph {
    char32_t ch;
    std::string utf8;       // 为了打印方便，存一份 UTF-8
    std::uint32_t color;    // 错：颜色每个字可能不同，是外部状态
};

class GlyphFactory {
public:
    Glyph& get(char32_t ch, const char* utf8) {  // 错：返回了非 const 引用，谁都能改
        auto it = cache_.find(ch);
        if (it == cache_.end()) it = cache_.emplace(ch, Glyph{ch, utf8, 0xFFFFFF}).first;
        return it->second;
    }
private:
    std::unordered_map<char32_t, Glyph> cache_;
};

void print(const char* label, const std::vector<Glyph*>& line) {
    std::printf("%s", label);
    for (auto* g : line) std::printf(" %s%s", g->utf8.c_str(), g->color == 0xFFFFFF ? "" : "(黄)");
    std::printf("\n");
}

int main() {
    GlyphFactory f;
    std::vector<Glyph*> line1{&f.get(U'我', "我"), &f.get(U'们', "们"), &f.get(U'走', "走"), &f.get(U'吧', "吧")};
    std::vector<Glyph*> line2{&f.get(U'我', "我"), &f.get(U'不', "不"), &f.get(U'走', "走")};

    // 卡拉 OK：第 1 行唱到第 1 个字，把它染成黄色
    line1[0]->color = 0xFFD700;
    print("第 1 行：", line1);
    print("第 2 行：", line2);  // 第 2 行还没开始唱，它的「我」也黄了
}
```

```
第 1 行： 我(黄) 们 走 吧
第 2 行： 我(黄) 不 走
```

第 2 行还没开始唱，它的「我」也黄了。这里犯了两个错：颜色是外部状态，却放进了享元；工厂返回了非 const 引用，让使用者能改享元。修正的办法也是两条：颜色挪到 `PlacedGlyph` 里，工厂返回 `const Glyph&`。这样再有人想改享元，编译器就会拦住：

<!-- run: off -->
```cpp
// 《享元模式》：修正——工厂返回 const Glyph&，颜色放在每个字的外部状态里；再想改享元就编译不过
#include <cstdint>
#include <unordered_map>

struct Glyph {
    char32_t ch;
    int advance;
};

class GlyphFactory {
public:
    const Glyph& get(char32_t ch) {
        auto it = cache_.find(ch);
        if (it == cache_.end()) it = cache_.emplace(ch, Glyph{ch, 48}).first;
        return it->second;
    }
private:
    std::unordered_map<char32_t, Glyph> cache_;
};

struct PlacedGlyph {
    const Glyph* glyph;
    float x, y;
    std::uint32_t color;  // 外部状态：每个字自己的颜色
};

int main() {
    GlyphFactory f;
    PlacedGlyph a{&f.get(U'我'), 0, 0, 0xFFFFFF};
    a.color = 0xFFD700;        // 对：只改这一个字
    a.glyph->advance = 60;     // 编译错误：不能通过 const Glyph* 修改享元
}
```

```
clang：error: cannot assign to non-static data member 'glyph' with const-qualified type 'const Glyph *'
gcc：  error: assignment of member 'Glyph::advance' in read-only object
```

判断一个字段是内部状态还是外部状态，问一句：**两个使用者用同一个享元时，这个字段会不会不一样？** 会，就是外部状态。

### 3.2 真实世界里的享元

- **字幕和字体渲染**：libass（mpv、VLC 等播放器用它渲染 ASS 字幕）内部有好几层缓存：字体、字形轮廓、位图、合成后的图像，键就是字体、字号、字和各种渲染参数；`ass_set_cache_limits()` 可以设置缓存的上限。FreeType 自带一个缓存子系统（`FTC_Manager`、`FTC_ImageCache`、`FTC_SBitCache`），做的是同一件事。浏览器和游戏引擎更进一步，把用过的字形画进一张纹理图集（glyph atlas），每个字只记它在图集里的坐标。
- **小整数和字符串**：Java 的 `Integer.valueOf()` 缓存了 -128～127，所以两个值为 127 的 `Integer` 用 `==` 比较是 `true`，128 就是 `false`。Python 缓存了 -5～256 的整数，`sys.intern()` 可以把字符串驻留下来。
- **Boost.Flyweight**：`boost::flyweight<std::string>` 用起来就像一个 `std::string`，背后自动查重、引用计数，还能通过模板参数选择工厂、加锁方式和回收策略。

### 3.3 字符串驻留

字符串驻留（interning）是享元最常见的形式：内容相同的字符串只存一份，其他地方只保存一个指向它的 `string_view` 或指针。视频剪辑软件的时间轴上可能有成千上万个片段，每个片段带一段滤镜描述，但实际只有几种：

```cpp
// 《享元模式》：字符串驻留——时间轴上 10 万个片段的滤镜描述只有 6 种，每种只存一份，片段里只放 string_view
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

static std::size_t g_allocs = 0, g_live = 0;
void* operator new(std::size_t n) {
    ++g_allocs;
    g_live += n;
    auto* h = static_cast<std::size_t*>(std::malloc(n + 16));
    if (!h) std::abort();
    *h = n;
    return reinterpret_cast<char*>(h) + 16;
}
void operator delete(void* p) noexcept {
    if (!p) return;
    auto* h = reinterpret_cast<std::size_t*>(static_cast<char*>(p) - 16);
    g_live -= *h;
    std::free(h);
}
void operator delete(void* p, std::size_t) noexcept { operator delete(p); }

// 驻留池：同样内容的字符串只存一份，返回指向它的 string_view
class StringPool {
public:
    std::string_view intern(std::string_view s) {
        auto it = pool_.find(s);  // 用 string_view 查找，不用为了查找先构造一个 std::string
        if (it != pool_.end()) return it->first;
        auto owned = std::make_unique<std::string>(s);
        std::string_view key = *owned;  // 键指向 unique_ptr 管的字符串，map 扩容时字符串不会搬家
        pool_.emplace(key, std::move(owned));
        return key;
    }
    std::size_t size() const { return pool_.size(); }

private:
    std::unordered_map<std::string_view, std::unique_ptr<std::string>> pool_;
};

const char* const kFilters[] = {
    "scale=1920:1080:flags=bicubic,format=yuv420p",
    "scale=1280:720:flags=bicubic,format=yuv420p",
    "crop=1920:800:0:140,scale=1920:1080,format=yuv420p",
    "eq=brightness=0.06:contrast=1.10:saturation=1.20",
    "fade=t=in:st=0:d=0.5,fade=t=out:st=4.5:d=0.5",
    "hflip,scale=1920:1080:flags=lanczos,format=yuv420p",
};

struct Clip {  // 不共享：每个片段自己存一份滤镜描述
    std::int64_t start_us, dur_us;
    std::string filter;
};
struct InternedClip {  // 驻留：片段里只放一个 string_view
    std::int64_t start_us, dur_us;
    std::string_view filter;
};

int main() {
    const int N = 100000;
    std::uint32_t s = 7;
    std::vector<int> which(N);  // 第 i 个片段用第几种滤镜
    for (auto& w : which) { s = s * 1664525u + 1013904223u; w = (s >> 8) % 6; }
    std::printf("sizeof(std::string) = %zu，sizeof(std::string_view) = %zu\n",
                sizeof(std::string), sizeof(std::string_view));

    std::size_t a0 = g_allocs, m0 = g_live;
    std::vector<Clip> clips;
    clips.reserve(N);
    for (int i = 0; i < N; ++i) clips.push_back({i * 40000LL, 40000, kFilters[which[i]]});
    std::printf("不共享：堆分配 %zu 次，占用 %.2f MB\n", g_allocs - a0, (g_live - m0) / 1048576.0);

    a0 = g_allocs, m0 = g_live;
    StringPool pool;
    std::vector<InternedClip> iclips;
    iclips.reserve(N);
    for (int i = 0; i < N; ++i) iclips.push_back({i * 40000LL, 40000, pool.intern(kFilters[which[i]])});
    std::printf("驻留：  堆分配 %zu 次，占用 %.2f MB（池里 %zu 个字符串）\n",
                g_allocs - a0, (g_live - m0) / 1048576.0, pool.size());

    // 找出所有用了第 2 种滤镜的片段：不共享时要比较字符串内容，驻留以后比较地址就够了
    using Clock = std::chrono::steady_clock;
    std::string target = kFilters[1];
    std::string_view itarget = pool.intern(target);
    int n1 = 0, n2 = 0;
    auto t0 = Clock::now();
    for (int r = 0; r < 100; ++r)
        for (auto& c : clips) n1 += (c.filter == target);
    auto t1 = Clock::now();
    for (int r = 0; r < 100; ++r)
        for (auto& c : iclips) n2 += (c.filter.data() == itarget.data());
    auto t2 = Clock::now();
    auto ns = [](auto a, auto b) { return std::chrono::duration<double, std::nano>(b - a).count() / (100.0 * N); };
    std::printf("按滤镜查找：比较内容 %.2f ns/个，比较地址 %.2f ns/个（都找到 %d 个）\n",
                ns(t0, t1), ns(t1, t2), n1 / 100 == n2 / 100 ? n1 / 100 : -1);
}
```

| 10 万个片段 | Clang / macOS | GCC 13 / Ubuntu |
|---|---|---|
| `sizeof(std::string)` | 24 | 32 |
| 不共享：堆分配 / 占用 | 100001 次 / 8.77 MB | 100001 次 / 9.11 MB |
| 驻留：堆分配 / 占用 | 22 次 / 3.05 MB | 20 次 / 3.05 MB |
| 按滤镜查找：比较内容 | 1.81 ns/个 | 0.88 ns/个 |
| 按滤镜查找：比较地址 | 0.46 ns/个 | 0.33 ns/个 |

- 10 万次分配变成了约 20 次：6 个 `std::string` 对象和它们的字符缓冲区、6 个 map 节点，再加上 map 的桶数组和片段数组。剩下的 3 MB 几乎全是片段数组本身（每个片段 32 字节）。
- **驻留以后判断相等只要比较地址**，这正是编译器、数据库给标识符和关键字做驻留的主要原因。
- 驻留池的键是 `string_view`，指向 `unique_ptr` 管着的字符串，所以 map 扩容时字符串不会搬家。这样做还有一个好处：C++17 的 `unordered_map<std::string, …>::find()` 只接受 `std::string`，用 `string_view` 去查得先构造一个临时字符串（可能要分配内存）；键本身就是 `string_view` 就没有这个问题。C++20 起可以给哈希和比较器加 `is_transparent`，直接用 `string_view` 查 `std::string` 键（异构查找）。
- 驻留池要比所有 `string_view` 活得长。驻留池通常是全局的，或者和使用它的文档、项目同生共死。

### 3.4 多线程下的工厂

享元本身不可变，多个线程同时读是安全的；**工厂不是**：两个线程同时 `get()` 一个没缓存过的字，会同时往 `unordered_map` 里插入，这是数据竞争。所以多线程共用一个工厂就要加锁，而 `get()` 是一个极频繁的调用。`mt_factory.cpp` 比较了几种做法（代码只在 dp-lab 里，用到了线程，浏览器里跑不了）：

<figure class="diagram">
<svg viewBox="0 0 720 216" role="img" aria-label="多线程下每次 get 的墙钟时间：Clang（12 核）不加锁 2.3 ns、mutex 单线程 4.5、mutex 4 线程 43.7、shared_mutex 4 线程 63.5、线程局部缓存 4 线程 16.1；GCC（2 核）分别是 1.8、10.0、31.5、27.6、16.1"><g style="font-family:var(--mono)"><text x="178.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang / macOS（12 核）</text><rect x="178.0" y="30.0" width="5.9" height="20.0" rx="2" style="fill:var(--link)"/><text x="189.9" y="45.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">2.3 ns</text><rect x="178.0" y="60.0" width="11.6" height="20.0" rx="2" style="fill:var(--accent)"/><text x="195.6" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">4.5 ns</text><rect x="178.0" y="90.0" width="112.4" height="20.0" rx="2" style="fill:var(--warn)"/><text x="296.4" y="105.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">43.7 ns</text><rect x="178.0" y="120.0" width="163.3" height="20.0" rx="2" style="fill:var(--danger)"/><text x="347.3" y="135.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">63.5 ns</text><rect x="178.0" y="150.0" width="41.4" height="20.0" rx="2" style="fill:var(--accent)"/><text x="225.4" y="165.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">16.1 ns</text><text x="448.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC 13 / Ubuntu（2 核虚拟机）</text><rect x="448.0" y="30.0" width="4.6" height="20.0" rx="2" style="fill:var(--link)"/><text x="458.6" y="45.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">1.8 ns</text><rect x="448.0" y="60.0" width="25.7" height="20.0" rx="2" style="fill:var(--accent)"/><text x="479.7" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">10 ns</text><rect x="448.0" y="90.0" width="81.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="535.0" y="105.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">31.5 ns</text><rect x="448.0" y="120.0" width="71.0" height="20.0" rx="2" style="fill:var(--danger)"/><text x="525.0" y="135.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">27.6 ns</text><rect x="448.0" y="150.0" width="41.4" height="20.0" rx="2" style="fill:var(--accent)"/><text x="495.4" y="165.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">16.1 ns</text><text x="170.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">不加锁，1 线程</text><text x="170.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">mutex，1 线程</text><text x="170.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">mutex，4 线程</text><text x="170.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">shared_mutex，4 线程</text><text x="170.0" y="165.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">线程局部缓存，4 线程</text><text x="10.0" y="208.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">mt_factory.cpp：工厂先预热（全部命中），再共查 40 × 19440 次，总墙钟时间 ÷ 总次数；-O2，7 次取中位数</text></g></svg>
</figure>

- **一把 mutex**：单线程时每次 `get()` 从 2.3 ns 变成 4.5 ns，还能接受；4 个线程一起查时变成 43.7 ns（墙钟时间），比单线程不加锁慢了近 20 倍。4 个线程全挤在一把锁上，同一时间只有一个在干活，还要加上锁在核之间来回传递的开销。
- **读写锁（`shared_mutex`）**：命中时只加共享锁，看起来很适合「读多写少」，但在 macOS 上反而更慢（63.5 ns），在 GCC 的 2 核虚拟机上比 mutex 稍快（27.6 ns 对 31.5 ns）。加共享锁也要原子地修改锁里的读者计数，临界区只有几纳秒时，这个计数本身就成了争抢的对象。
- **线程局部缓存**：每个线程先查自己的一张小表（不用加锁），查不到再去加锁的共享工厂，然后把地址记进小表。两边都是 16.1 ns。每个线程只在第一次遇到一个字时加锁，以后都不用；这里没能更快，主要是每个线程都要把 2315 个字各查一遍共享工厂，再填满自己的表。

<!-- run: off -->
```cpp
// mt_factory.cpp 节选：线程局部缓存
const Glyph& get(char32_t ch) {
    struct Local {
        const LocalCacheFactory* owner = nullptr;
        std::unordered_map<char32_t, const Glyph*> map;
    };
    thread_local Local local;
    if (local.owner != this) { local.map.clear(); local.owner = this; }  // 换了工厂，旧指针作废
    auto it = local.map.find(ch);
    if (it != local.map.end()) return *it->second;
    const Glyph& g = shared_.get(ch);   // 加锁的共享工厂
    local.map.emplace(ch, &g);
    return g;
}
```

更简单的做法是**不共享工厂**：让每个渲染线程有自己的工厂，或者在单线程里排版好，再把只读的结果交给多个线程去画。享元不可变，读多少次都不用加锁，要锁的只是「创建」这一步。

### 3.5 什么时候不值得

享元有代价，GoF 列出的适用条件是同时满足下面几条：

- 对象数量**非常多**，多到内存或创建的开销真的成了问题；
- 对象的大部分状态可以变成外部状态；
- 去掉外部状态以后，很多对象可以用少量共享的对象代替；
- 程序不依赖对象的身份（不会用地址区分两个「我」）。

代价在于：每次使用都要多带一份外部状态（`draw(glyph, x, y, color)` 而不是 `glyph.draw()`），代码更绕；工厂多了一次查找，多线程下还要加锁；工厂一直持有所有享元，用过一次的字形也不会释放。如果只有几百个对象，或者对象之间几乎没有重复，就不要用。

## 4. 现代 C++ 写法

### 4.1 工厂返回什么

| 返回类型 | 大小 | 享元的寿命 | 代价 |
|---|---|---|---|
| `const Glyph&` / `const Glyph*` | 8 字节 | 跟工厂一样长 | 工厂必须比所有使用者活得长 |
| `std::shared_ptr<const Glyph>` | 16 字节 | 最后一个使用者放手时 | 每次拷贝、销毁都要原子地加减引用计数 |
| 下标或编号（`uint32_t`） | 4 字节 | 跟工厂一样长 | 每次使用都要回工厂查一次；但存储可以是 `vector`，不怕搬家 |

前面一直用的是第一种：享元由工厂独占，使用者只借用。如果工厂的寿命不好保证（比如享元要跨模块、跨线程传递），可以换成 `shared_ptr<const Glyph>`，`const` 同样保证了享元不能改。第三种在游戏引擎和数据导向的设计里很常见：一个字形编号只有 4 字节，`PlacedGlyph` 从 16 字节变成 12 字节。

### 4.2 用 weak_ptr 记住享元

工厂一直持有所有享元，内存只增不减。一个自然的想法是让工厂只用 `weak_ptr` 记住享元：使用者持有 `shared_ptr`，最后一个使用者放手时享元就自动释放。实际效果如何？模拟播放这条字幕，屏幕上同时显示两行，离开屏幕的那一行放开它的字形：

```cpp
// 《享元模式》：工厂返回 shared_ptr<const Glyph>、只用 weak_ptr 记住享元——没人用的字形自动释放，代价是重新光栅化
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// ---- 一条 90 分钟电影的中文字幕：1400 行，每行 8～20 个字，用字频率按 Zipf 分布 ----
std::vector<std::u32string> make_track(int lines) {
    const int kPool = 3000;  // 3000 个常用字，第 k 常用的字出现的频率 ∝ 1/k
    std::vector<double> cdf(kPool);
    double sum = 0;
    for (int k = 0; k < kPool; ++k) cdf[k] = sum += 1.0 / (k + 1);
    std::uint32_t s = 2026;
    auto rnd = [&s] { s = s * 1664525u + 1013904223u; return s >> 8; };
    std::vector<std::u32string> track;
    for (int i = 0; i < lines; ++i) {
        std::u32string text;
        int len = 8 + rnd() % 13;
        for (int j = 0; j < len; ++j) {
            double u = (rnd() % 1000000) / 1e6 * sum;
            text += char32_t(0x4E00 + (std::lower_bound(cdf.begin(), cdf.end(), u) - cdf.begin()));
        }
        track.push_back(text);
    }
    return track;
}

// 模拟 FreeType 把一个字的轮廓画成 px×px 的 8 位灰度位图
static int g_raster = 0;
std::vector<std::uint8_t> rasterize(char32_t ch, int px) {
    ++g_raster;
    std::vector<std::uint8_t> bm(px * px);
    std::uint32_t h = ch * 2654435761u;
    for (auto& b : bm) { h ^= h << 13; h ^= h >> 17; h ^= h << 5; b = std::uint8_t(h); }
    return bm;
}

struct Glyph {
    char32_t ch;
    std::vector<std::uint8_t> bitmap;
    static inline int alive = 0, peak = 0;
    Glyph(char32_t c, std::vector<std::uint8_t> b) : ch(c), bitmap(std::move(b)) { peak = std::max(peak, ++alive); }
    ~Glyph() { --alive; }
    Glyph(const Glyph&) = delete;
    Glyph& operator=(const Glyph&) = delete;
};
const int px = 48;

// 强引用：工厂一直持有所有享元，直到工厂销毁
class StrongFactory {
public:
    std::shared_ptr<const Glyph> get(char32_t ch) {
        auto& slot = cache_[ch];
        if (!slot) slot = std::make_shared<const Glyph>(ch, rasterize(ch, px));
        return slot;
    }
    std::size_t entries() const { return cache_.size(); }
private:
    std::unordered_map<char32_t, std::shared_ptr<const Glyph>> cache_;
};

// 弱引用：工厂只记 weak_ptr，最后一个使用者放手时享元就释放了
class WeakFactory {
public:
    std::shared_ptr<const Glyph> get(char32_t ch) {
        auto& slot = cache_[ch];
        if (auto sp = slot.lock()) return sp;  // 还有人在用：直接共享
        auto sp = std::make_shared<const Glyph>(ch, rasterize(ch, px));
        slot = sp;
        return sp;
    }
    std::size_t entries() const { return cache_.size(); }
    std::size_t expired() const {
        return std::count_if(cache_.begin(), cache_.end(), [](auto& kv) { return kv.second.expired(); });
    }
private:
    std::unordered_map<char32_t, std::weak_ptr<const Glyph>> cache_;
};

// 播放整条字幕：屏幕上同时显示当前行和上一行（淡出中），每行持有自己那些字的 shared_ptr
template <class Factory>
void play(const std::vector<std::u32string>& track, Factory& f) {
    std::vector<std::shared_ptr<const Glyph>> shown[2];
    for (std::size_t i = 0; i < track.size(); ++i) {
        auto& cur = shown[i % 2];
        cur.clear();  // 两行以前的那一行离开屏幕，放开它的字形
        for (char32_t ch : track[i]) cur.push_back(f.get(ch));
    }
}

int main() {
    auto track = make_track(1400);
    std::printf("sizeof(const Glyph*) = %zu，sizeof(shared_ptr<const Glyph>) = %zu\n",
                sizeof(const Glyph*), sizeof(std::shared_ptr<const Glyph>));
    {
        StrongFactory f;
        play(track, f);
        std::printf("强引用缓存：光栅化 %d 次，同时存活的字形最多 %d 个，播完还有 %d 个\n",
                    g_raster, Glyph::peak, Glyph::alive);
    }
    g_raster = 0;
    Glyph::peak = 0;
    {
        WeakFactory f;
        play(track, f);
        std::printf("weak_ptr 缓存：光栅化 %d 次，同时存活的字形最多 %d 个，播完还有 %d 个\n",
                    g_raster, Glyph::peak, Glyph::alive);
        std::printf("               map 里有 %zu 个条目，其中 %zu 个已经过期（控制块还没释放）\n",
                    f.entries(), f.expired());
    }
}
```

```
sizeof(const Glyph*) = 8，sizeof(shared_ptr<const Glyph>) = 16
强引用缓存：光栅化 2315 次，同时存活的字形最多 2315 个，播完还有 2315 个
weak_ptr 缓存：光栅化 14829 次，同时存活的字形最多 37 个，播完还有 0 个
               map 里有 2315 个条目，其中 2315 个已经过期（控制块还没释放）
```

两边结果相同。内存确实省到了极致：同时只有 37 个字形。可是光栅化从 2315 次涨到了 14829 次，几乎回到了不共享的 19440 次：一个字离开屏幕就被释放，下次再出现又要重新画。另外，过期的 `weak_ptr` 还留在 map 里，它们的控制块（用 `make_shared` 创建时，连同 `Glyph` 对象本身的那块内存）要等 `weak_ptr` 被删掉才释放，需要定期清理。

所以真正的字形缓存（比如 libass）用的是**有容量上限的缓存**：最近用过的留着，满了再淘汰最久没用的，见练习 2。

### 4.3 看起来像享元的东西

有几个东西看起来像享元，但不是：

- **短字符串优化**：把短字符串存在 `std::string` 对象自己里面，是为了少分配，不是共享，两个 `"abc"` 还是两份。
- **写时复制（COW）**：Qt 的 `QString`、GCC 5 以前 libstdc++ 的 `std::string`，拷贝时先共享同一份数据，有人修改时再复制。它共享的是可变的数据，并且对使用者透明；享元共享的是不可变的数据，使用者明确知道自己在共享。
- **`std::string_view`、`std::span`**：它们只是「借用」别人的数据，本身不负责查重和共享。和驻留池配合起来才是享元。

## 5. 享元和相似的模式

| | 享元 | 单例 | 原型 | 对象池 |
|---|---|---|---|---|
| 解决的问题 | 大量相似对象占太多内存 | 某个类只能有一个实例 | 复制一个只拿到基类指针的对象 | 创建、销毁对象太贵 |
| 每种对象有几个实例 | 每个「键」一个，可以有成千上万种 | 整个程序一个 | 想复制多少就多少 | 池子的容量 |
| 同时被几个使用者用 | 很多个，一直共享 | 很多个 | 一个，复制品归使用者所有 | 一个，用完还回池子 |
| 对象可不可以改 | 不可以 | 可以 | 可以，复制品各改各的 | 可以，还回去前要重置 |
| 典型例子 | 字形缓存、字符串驻留 | 日志、配置 | 图层、关卡模板 | 线程池、连接池、AVFrame 池 |

- **享元和单例**：享元工厂往往是程序里唯一的一个，容易写成单例，但享元本身是「每个键一个」，不是「整个类一个」。工厂最好也作为参数传进去（依赖注入），测试时可以换一个空的。
- **享元和原型**：方向正好相反。原型是从一个对象复制出新的、独立的对象；享元是让本来要各自创建的对象合并成一个。
- **享元和对象池**：都是「复用对象」，但对象池里的对象同一时间只给一个使用者，用完还回去，下一个使用者拿到时状态要重置；享元同一时间被很多使用者共享，从来不还、也不能改。FFmpeg 的 `AVBufferPool`（解码器用它复用帧缓冲区）是对象池，不是享元。
- **享元和组合**：GoF 书里文字编辑器的例子同时用了两者：文档是一棵组合树（行、列、字符），叶子节点「字符」是享元。

## 6. 小结

- 享元模式把对象的状态分成两部分：**内部状态**可以共享，存在享元里；**外部状态**因使用场合而异，由使用者保存。判断方法：两个使用者用同一个享元时，这个字段会不会不一样。
- **工厂**按内部状态（键要完整）缓存享元，同样的键只创建一次。实测一条中文字幕 19440 个字只有 2315 种，内存从 44.4 MB 降到 5.6 MB，排版快了约 8 倍；工厂查一次只要几纳秒。
- **享元必须不可变**：工厂返回 `const` 引用或 `shared_ptr<const T>`，让编译器拦住修改。
- **享元不能搬家**：使用者保存了享元的地址，工厂用 `unordered_map`、`deque` 或 `unique_ptr` 存享元，不要用 `vector`，或者干脆让使用者只存编号。
- **多线程下工厂要加锁**，4 个线程挤一把锁，每次查找慢了近 20 倍；用线程局部缓存，或者每个线程一个工厂。
- 字符串驻留是最常见的享元：省内存，判断相等只比较地址。

## 7. 练习

1. **辨析**：视频剪辑软件的时间轴上有 5 万个片段，每个片段有下面这些字段。如果要用享元模式，哪些是内部状态，哪些是外部状态？
   素材文件路径、视频的宽高和像素格式、编码格式、片段在时间轴上的起始时间、从素材的第几秒开始截取（入点）、截取多长、音量、所在的轨道、滤镜描述。

2. **动手**：第 4.2 节的 `weak_ptr` 缓存释放得太早，强引用缓存又从不释放。给字形缓存加一个容量上限，满了按 LRU（最久没用的先淘汰）扔掉一个。模拟播放这条字幕：每行显示 3 秒、25 帧，每一帧都把这一行的字重画一遍。容量分别为 16、64、256、1024、4096 时，要光栅化多少次？

3. **设计**：ASS 字幕文件里有一个样式表（`[V4+ Styles]`），每一行台词引用一种样式（字体、字号、颜色、描边宽度、粗体、对齐方式……），台词里还可以用 `{\b1}`、`{\c&H00FFFF&}` 这样的标签临时覆盖样式的某一项。怎样用享元来存这 1400 行台词的样式？带了覆盖标签的台词怎么办？

4. **找问题**：下面这个工厂用 `vector` 连续地存享元，看起来又省内存又对缓存友好。它有什么问题？怎么改？

   <!-- run: off -->
   ```cpp
   // 《享元模式》练习 4：找问题——工厂把享元存在 vector 里，返回元素的引用；字多了以后先拿到的引用全部悬空
   #include <cstdio>
   #include <unordered_map>
   #include <vector>

   struct Glyph {
       char32_t ch;
       int advance;
   };

   class GlyphFactory {
   public:
       const Glyph& get(char32_t ch) {
           auto it = index_.find(ch);
           if (it != index_.end()) return glyphs_[it->second];
           index_[ch] = glyphs_.size();
           glyphs_.push_back(Glyph{ch, 48});  // 容量不够时，vector 会搬到一块新内存
           return glyphs_.back();
       }
       std::size_t size() const { return glyphs_.size(); }

   private:
       std::vector<Glyph> glyphs_;                     // 享元连续存放，看起来很省
       std::unordered_map<char32_t, std::size_t> index_;  // 字 → 下标
   };

   int main() {
       GlyphFactory f;
       const Glyph* first = &f.get(U'一');
       std::printf("第 1 个字形在 %p\n", static_cast<const void*>(first));
       for (char32_t ch = 0x4E01; ch < 0x4E01 + 100; ++ch) f.get(ch);  // 后面又来了 100 个新字
       std::printf("现在第 1 个字形在 %p\n", static_cast<const void*>(&f.get(U'一')));
       std::printf("first->ch = U+%04X，advance = %d\n", unsigned(first->ch), first->advance);  // 读的是已经释放的内存
   }
   ```

<details>
<summary>点击查看答案</summary>

**1.** 用那个判断方法：两个片段用同一个享元时，这个字段会不会不一样。

- **内部状态**：素材文件路径、视频的宽高和像素格式、编码格式。这些只取决于「是哪个素材」，同一个素材被切成 100 段放在时间轴上，这些信息都一样。可以做一个 `MediaInfo` 享元（还可以把解码器的配置、缩略图也放进去），按文件路径缓存。
- **外部状态**：在时间轴上的起始时间、入点、截取长度、音量、所在的轨道。每个片段都不一样。
- **滤镜描述**两种都可能：它不取决于素材，所以不能放进 `MediaInfo`；但它的取值只有几种，可以像第 3.3 节那样单独驻留，片段里存一个 `string_view`。

于是一个片段是 `{const MediaInfo* media; std::string_view filter; int64_t start, in, dur; float volume; int track;}`。真实的剪辑软件就是这样：同一个素材文件只导入、分析一次，时间轴上的片段都引用它。

**2.** 用一个 `list` 按最近使用的顺序放字形，再用 `unordered_map` 从字找到它在 `list` 里的位置。命中时用 `splice` 把它挪到最前面（不复制、不搬家），满了就扔掉最后一个：

```cpp
// 《享元模式》练习 2：给字形缓存加容量上限，满了按 LRU（最久没用的先扔）淘汰；比较不同容量要光栅化多少次
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

// ---- 一条 90 分钟电影的中文字幕：1400 行，每行 8～20 个字，用字频率按 Zipf 分布 ----
std::vector<std::u32string> make_track(int lines) {
    const int kPool = 3000;  // 3000 个常用字，第 k 常用的字出现的频率 ∝ 1/k
    std::vector<double> cdf(kPool);
    double sum = 0;
    for (int k = 0; k < kPool; ++k) cdf[k] = sum += 1.0 / (k + 1);
    std::uint32_t s = 2026;
    auto rnd = [&s] { s = s * 1664525u + 1013904223u; return s >> 8; };
    std::vector<std::u32string> track;
    for (int i = 0; i < lines; ++i) {
        std::u32string text;
        int len = 8 + rnd() % 13;
        for (int j = 0; j < len; ++j) {
            double u = (rnd() % 1000000) / 1e6 * sum;
            text += char32_t(0x4E00 + (std::lower_bound(cdf.begin(), cdf.end(), u) - cdf.begin()));
        }
        track.push_back(text);
    }
    return track;
}

// 模拟 FreeType 把一个字的轮廓画成 px×px 的 8 位灰度位图
static int g_raster = 0;
std::vector<std::uint8_t> rasterize(char32_t ch, int px) {
    ++g_raster;
    std::vector<std::uint8_t> bm(px * px);
    std::uint32_t h = ch * 2654435761u;
    for (auto& b : bm) { h ^= h << 13; h ^= h >> 17; h ^= h << 5; b = std::uint8_t(h); }
    return bm;
}

struct Glyph {
    char32_t ch;
    std::vector<std::uint8_t> bitmap;
};
const int px = 48;

class LruGlyphCache {
public:
    explicit LruGlyphCache(std::size_t capacity) : cap_(capacity) {}
    // 返回的引用只保证用到下一次 get 之前：再 get 可能把它淘汰掉
    const Glyph& get(char32_t ch) {
        auto it = index_.find(ch);
        if (it != index_.end()) {
            order_.splice(order_.begin(), order_, it->second);  // 移到最前面（最近用过），不复制
            return *it->second;
        }
        if (order_.size() == cap_) {  // 满了：扔掉最后面那个（最久没用的）
            index_.erase(order_.back().ch);
            order_.pop_back();
        }
        order_.push_front(Glyph{ch, rasterize(ch, px)});
        index_[ch] = order_.begin();
        return order_.front();
    }

private:
    std::size_t cap_;
    std::list<Glyph> order_;  // 前面是最近用过的
    std::unordered_map<char32_t, std::list<Glyph>::iterator> index_;
};

int main() {
    auto track = make_track(1400);
    const int kFrames = 75;  // 每行字幕显示 3 秒，25 fps，每帧都要画一遍
    std::size_t gets = 0;
    for (auto& t : track) gets += t.size() * kFrames;
    std::printf("不缓存：光栅化 %zu 次\n", gets);
    for (std::size_t cap : {16, 64, 256, 1024, 4096}) {
        g_raster = 0;
        LruGlyphCache cache(cap);
        long long sum = 0;
        for (auto& t : track)
            for (int frame = 0; frame < kFrames; ++frame)
                for (char32_t ch : t) sum += cache.get(ch).bitmap[0];  // 拿到就画，不保存引用
        std::printf("容量 %4zu：光栅化 %6d 次（校验和 %lld）\n", cap, g_raster, sum);
    }
}
```

```
不缓存：光栅化 1458000 次
容量   16：光栅化 205454 次（校验和 174689100）
容量   64：光栅化  11418 次（校验和 174689100）
容量  256：光栅化   7520 次（校验和 174689100）
容量 1024：光栅化   3726 次（校验和 174689100）
容量 4096：光栅化   2315 次（校验和 174689100）
```

Clang 和 GCC 结果相同。几个现象：

- **容量 16 时效果很差**：一行字幕最多 20 个字，不同的字超过 16 个时，画到第 17 个字，第 1 个字刚好被淘汰；下一帧又从第 1 个字画起，又把第 2 个字淘汰……按顺序反复访问一组比容量稍大的数据，是 LRU 最怕的情况，这样的行每一帧都全部不命中。
- **容量 64 时已经比不缓存少了 99%**：同一行的 75 帧里只有第一帧要光栅化。剩下的差别来自不同行之间共用的字，容量越大，留下来的常用字越多。
- **容量 ≥ 不同字的数量（2315）时**，和永不淘汰的工厂一样。
- 有淘汰就有一个新问题：**`get()` 返回的引用只能用到下一次 `get()` 之前**，因为下一次可能把它淘汰掉。所以这里的用法是「拿到就画，不保存」。要让使用者长期持有享元，就要换成 `shared_ptr`：缓存淘汰只是放开自己的那份引用，还有人在用的字形不会被释放。

**3.** 一种样式就是一个享元：字体、字号、颜色、描边这些都是内部状态，台词只保存一个指向样式的指针，再加上自己的时间和文字（外部状态）。

带覆盖标签的台词，不要去改它引用的样式（享元不能改，否则所有用这个样式的台词都变了），而是**复制一份样式，改掉被覆盖的那一项，再放进池子里驻留**：内容和已有的某个样式相同时，拿到的就是同一个享元。所以样式池要按「值」查重，而不是按样式名：

```cpp
// 《享元模式》练习 3：ASS 字幕的样式——1400 行台词只引用几种样式；行内覆盖（\b1、\c）生成的新样式也驻留起来
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

struct Style {  // 内部状态：同一种样式的所有台词都一样
    std::string font;
    int size;
    std::uint32_t primary, outline;
    float outline_w;
    bool bold, italic;
    int align;
    bool operator==(const Style& o) const {
        return font == o.font && size == o.size && primary == o.primary && outline == o.outline &&
               outline_w == o.outline_w && bold == o.bold && italic == o.italic && align == o.align;
    }
};
struct StyleHash {
    std::size_t operator()(const Style& s) const {
        std::size_t h = std::hash<std::string>{}(s.font);
        for (std::size_t v : {std::size_t(s.size), std::size_t(s.primary), std::size_t(s.outline),
                              std::size_t(s.outline_w * 100), std::size_t(s.bold), std::size_t(s.italic),
                              std::size_t(s.align)})
            h = h * 1000003 ^ v;
        return h;
    }
};

// 样式池：按「值」驻留，内容相同的样式只存一份
class StylePool {
public:
    const Style* intern(const Style& s) { return &*pool_.insert(s).first; }  // unordered_set 的元素不会搬家
    std::size_t size() const { return pool_.size(); }
private:
    std::unordered_set<Style, StyleHash> pool_;
};

struct Dialogue {  // 外部状态：每行台词自己的时间和文字
    int start_ms, end_ms;
    std::string text;
    const Style* style;
};

int main() {
    StylePool pool;
    const Style* def = pool.intern({"Noto Sans CJK SC", 48, 0xFFFFFF, 0x000000, 2.5f, false, false, 2});
    const Style* top = pool.intern({"Noto Sans CJK SC", 40, 0xFFFFFF, 0x000000, 2.0f, false, false, 8});
    const Style* sign = pool.intern({"Source Han Serif SC", 56, 0x00FFFF, 0x202020, 3.0f, true, false, 5});

    std::vector<Dialogue> lines;
    std::uint32_t s = 11;
    auto rnd = [&s] { s = s * 1664525u + 1013904223u; return (s >> 8) % 100; };
    for (int i = 0; i < 1400; ++i) {
        const Style* base = i % 50 == 0 ? sign : (i % 7 == 0 ? top : def);
        int r = rnd();
        if (r < 5) {                         // 5% 的台词有 {\b1}：在基础样式上加粗
            Style st = *base; st.bold = true; base = pool.intern(st);
        } else if (r < 10) {                 // 5% 有 {\c&H00FFFF&}：改成黄色
            Style st = *base; st.primary = 0x00FFFF; base = pool.intern(st);
        }
        lines.push_back({i * 3500, i * 3500 + 3000, "……", base});
    }
    std::printf("%zu 行台词，样式池里 %zu 种样式；每行只存一个 %zu 字节的指针，不用各存一份 %zu 字节的 Style\n",
                lines.size(), pool.size(), sizeof(const Style*), sizeof(Style));
}
```

```
1400 行台词，样式池里 7 种样式；每行只存一个 8 字节的指针，不用各存一份 48 字节的 Style
```

GCC 上 `Style` 是 56 字节（`std::string` 大 8 个字节）。3 种基础样式加上覆盖后产生的 4 种，一共 7 种。其中 `Sign` 本来就是黄色加粗，在它上面覆盖 `\b1` 或者黄色，得到的还是 `Sign` 自己。真正的 ASS 渲染器里，一行台词中间也可以切换样式，那时外部状态就变成了「从第几个字开始用哪个样式」的一串区间。

**4.** `vector` 的容量不够时，`push_back` 会分配一块更大的内存，把所有元素搬过去，再释放旧的内存。之前 `get()` 返回的引用、使用者存下来的 `const Glyph*` 全都指向了已经释放的内存。运行结果：

```
第 1 个字形在 0x1014f9310
现在第 1 个字形在 0x1014fb340
first->ch = U+4E05，advance = 0
```

`first` 读到的是一块已经释放、又被别人重新用了的内存，读出了 U+4E05 和 0。打开 AddressSanitizer（`-fsanitize=address`）在 Clang 和 GCC 上都会报 `heap-use-after-free`，指向读 `first->ch` 的那一行。这个错误只在享元多到让 `vector` 扩容时才出现：只有几个字的测试发现不了它。

改法有几种：

- 享元放进**地址不变的容器**：`std::deque`（只在两端 `push_back`、`push_front` 时，已有元素的引用一直有效）、`std::unordered_map` 或 `std::map`（节点式容器），或者 `vector<unique_ptr<Glyph>>`（搬的只是指针）。
- 使用者**不存地址，只存下标**（第 4.1 节的第三种）：`vector` 怎么扩容都没关系，用的时候再 `glyphs_[id]`。
- 不要用 `reserve()` 补救：预留的容量一旦用完，问题照样出现。

改成 `deque` 的版本：

```cpp
// 《享元模式》练习 4 的修正：享元改存在 deque 里——push_back 只在两端加块，已有元素不搬家，引用一直有效
#include <cstdio>
#include <deque>
#include <unordered_map>

struct Glyph {
    char32_t ch;
    int advance;
};

class GlyphFactory {
public:
    const Glyph& get(char32_t ch) {
        auto it = index_.find(ch);
        if (it != index_.end()) return *it->second;
        glyphs_.push_back(Glyph{ch, 48});  // deque::push_back 不会让已有元素的引用失效
        index_[ch] = &glyphs_.back();
        return glyphs_.back();
    }

private:
    std::deque<Glyph> glyphs_;
    std::unordered_map<char32_t, const Glyph*> index_;
};

int main() {
    GlyphFactory f;
    const Glyph* first = &f.get(U'一');
    for (char32_t ch = 0x4E01; ch < 0x4E01 + 100; ++ch) f.get(ch);
    std::printf("第 1 个字形还在原地：%s\n", first == &f.get(U'一') ? "是" : "否");
    std::printf("first->ch = U+%04X，advance = %d\n", unsigned(first->ch), first->advance);
}
```

```
第 1 个字形还在原地：是
first->ch = U+4E00，advance = 48
```

</details>
