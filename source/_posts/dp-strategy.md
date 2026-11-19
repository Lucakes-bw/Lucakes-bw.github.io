---
title: 策略模式
date: 2026-11-11 22:00:00
updated: 2026-11-11 22:00:00
categories:
  - 设计模式
  - 行为型模式
tags:
  - 设计模式
  - 策略
  - 虚函数
  - std::function
  - std::variant
  - 视频缩放
order: 1
description: 播放器支持最近邻、双线性、双三次三种缩放算法：用 enum + switch 写，算法逻辑散在 4 个函数里，加一种 Lanczos 要改 7 处；改成策略模式后，新增一个类并在表里登记一行，已有类不动。用 bbb-10s.mp4 实测四种策略：720p→1080p 每帧 0.97～93.34 ms、画质 41.99～50.06 dB，所以需要在运行时切换（掉帧时自动降级）。再对比现代 C++ 四种写法（虚函数、std::function、模板、std::variant），实测说明调用开销取决于策略粒度而非多态写法本身，最后分清策略与状态、模板方法、桥接的区别。
top_group_list: true
disableNunjucks: true
viz: [dp-strategy]
---

> **本文目标**：理解策略模式——把一族可互换的算法封装成对象，让上下文在运行时按需替换；会把 `enum + switch` 重构成策略；掌握现代 C++ 四种策略写法（虚函数、`std::function`、模板参数、`std::variant`）及其适用场景；理解为什么「策略粒度」比「多态写法」更影响性能；分清策略与状态、模板方法、桥接。
>
> **前置知识**：《为什么需要设计模式》《组合优于继承与 C++ 的四种多态》。这是第 4 章「行为型模式」的第一篇：创建型关心对象怎么创建，结构型关心对象怎么组合，**行为型关心对象之间怎么分配职责、怎么协作**。
>
> **编译环境**：`g++ / clang++ -std=c++17`，完整 2D 缩放与性能压测代码见 `projects/dp-lab/18-strategy/`。

## 1. 问题：算法越加越多，`switch` 越散越广

### 1.1 用 `enum + switch` 选缩放算法

播放器把视频帧缩放到窗口大小时，通常支持多种算法：

- **最近邻**（nearest）：取最近的 1 个像素，最快但有锯齿；
- **双线性**（bilinear）：取周围 2×2 = 4 个像素加权，平滑且较快；
- **双三次**（bicubic）：取周围 4×4 = 16 个像素卷积，清晰但较慢。

最直接的写法是定义一个 `enum class Algo`，凡是和算法有关的地方都 `switch` 一下。下面用一行 8 个像素（中间第 4 格是一条亮线 `240`）的简化版本演示这种结构：

```cpp
// 《策略模式》：问题版——算法相关代码散落在解析、名字、采样数、插值 4 个函数里
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

enum class Algo { Nearest, Bilinear, Bicubic };

// ① 字符串 → 枚举
Algo parseAlgo(const std::string& s) {
    if (s == "nearest") return Algo::Nearest;
    if (s == "bicubic") return Algo::Bicubic;
    return Algo::Bilinear;
}

// ② 枚举 → 名字（日志、下拉框）
const char* algoName(Algo a) {
    switch (a) {
    case Algo::Nearest: return "nearest";
    case Algo::Bilinear: return "bilinear";
    case Algo::Bicubic: return "bicubic";
    }
    return "?";
}

// ③ 枚举 → 采样点数（估算开销）
int algoTaps(Algo a) {
    switch (a) {
    case Algo::Nearest: return 1;
    case Algo::Bilinear: return 4;
    case Algo::Bicubic: return 16;
    }
    return 0;
}

// ④ 在一行像素的浮点位置 x 处采样：所有算法挤在同一个 switch 里
int sample(const std::vector<int>& row, float x, Algo a) {
    auto at = [&](int i) { return row[std::clamp(i, 0, static_cast<int>(row.size()) - 1)]; };
    int x0 = static_cast<int>(std::floor(x));
    float fx = x - x0;
    switch (a) {
    case Algo::Nearest:
        return at(static_cast<int>(std::floor(x + 0.5f)));
    case Algo::Bilinear:
        return static_cast<int>(at(x0) * (1 - fx) + at(x0 + 1) * fx + 0.5f);
    case Algo::Bicubic: {
        auto cubic = [](float t) {
            t = std::fabs(t);
            return t < 1 ? (1.5f * t - 2.5f) * t * t + 1
                 : t < 2 ? ((-0.5f * t + 2.5f) * t - 4) * t + 2 : 0.f;
        };
        float sum = 0;
        for (int i = -1; i <= 2; ++i) sum += at(x0 + i) * cubic(fx - i);
        return std::clamp(static_cast<int>(sum + 0.5f), 0, 255);
    }
    }
    return 0;
}

int main() {
    std::vector<int> row = {20, 30, 40, 50, 240, 70, 80, 90};   // 中间有一条亮线 240
    for (std::string arg : {"nearest", "bilinear", "bicubic"}) {
        Algo a = parseAlgo(arg);
        std::printf("%-8s taps=%-2d 结果:", algoName(a), algoTaps(a));
        for (int x = 0; x < 12; ++x)
            std::printf(" %3d", sample(row, (x + 0.5f) * 8 / 12 - 0.5f, a));
        std::printf("\n");
    }
}
```

```text
nearest  taps=1  结果:  20  30  30  40  50  50 240  70  70  80  90  90
bilinear taps=4  结果:  20  25  32  38  45  82 208 155  72  78  85  90
bicubic  taps=16 结果:  19  24  32  38  34  75 227 166  61  76  86  91
```

### 1.2 加一种 Lanczos 要改几处

如果再加一种画质更好的 Lanczos 算法（`projects/dp-lab/18-strategy/switch_scaler_lanczos.cpp`），`measure.sh` 统计两种写法的改动如下：

<figure class="diagram">
<svg viewBox="0 0 750 354" role="img" aria-label="加一种 Lanczos 算法：switch 版要改 enum、parseAlgo、algoName、algoTaps、sample 和 main 六个已有的地方，再加一个辅助函数；策略版新增一个 Lanczos 类，已有的类不动，只在 makeScaler 的表里登记一行、main 的列表加一项"><g style="font-family:var(--mono)"><text x="20.0" y="20.0" text-anchor="start" style="fill:var(--fg);font-size:12.5px;font-weight:700">switch_scaler.cpp：改 6 个已有的地方</text><text x="400.0" y="20.0" text-anchor="start" style="fill:var(--fg);font-size:12.5px;font-weight:700">strategy_scaler.cpp：已有的类一个都不动</text><rect x="20" y="44" width="330" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="30.0" y="60.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">enum class Algo { …, Lanczos }</text><rect x="20" y="74" width="330" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="30.0" y="90.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">parseAlgo()  +1 个 if</text><rect x="20" y="104" width="330" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="30.0" y="120.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">algoName()  +1 个 case</text><rect x="20" y="134" width="330" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="30.0" y="150.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">algoTaps()  +1 个 case</text><rect x="20" y="164" width="330" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1.3"/><text x="30.0" y="180.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">cubic()</text><rect x="20" y="194" width="330" height="24" rx="3" style="fill:var(--accent);fill-opacity:.16;stroke:var(--accent);stroke-width:1.3"/><text x="30.0" y="210.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">lanczos3()  新的辅助函数</text><rect x="20" y="224" width="330" height="34" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="30.0" y="245.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">sample()  +1 个 case（10 行）</text><rect x="20" y="264" width="330" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1.3"/><text x="30.0" y="280.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">scale()</text><rect x="20" y="294" width="330" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="30.0" y="310.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">main()  命令行列表 +1</text><rect x="400" y="44" width="330" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1.3"/><text x="410.0" y="60.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">class Scaler（接口）</text><rect x="400" y="74" width="330" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1.3"/><text x="410.0" y="90.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">resample()</text><rect x="400" y="104" width="330" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1.3"/><text x="410.0" y="120.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">class Nearest</text><rect x="400" y="134" width="330" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1.3"/><text x="410.0" y="150.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">class Bilinear</text><rect x="400" y="164" width="330" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1.3"/><text x="410.0" y="180.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">class Bicubic</text><rect x="400" y="194" width="330" height="34" rx="3" style="fill:var(--accent);fill-opacity:.16;stroke:var(--accent);stroke-width:1.3"/><text x="410.0" y="215.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">class Lanczos  新增（28 行）</text><rect x="400" y="234" width="330" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="410.0" y="250.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">makeScaler()  表里登记 1 行</text><rect x="400" y="264" width="330" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1.3"/><text x="410.0" y="280.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">class Thumbnailer（上下文）</text><rect x="400" y="294" width="330" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="410.0" y="310.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">main()  命令行列表 +1</text><rect x="20.0" y="330.0" width="14.0" height="12.0" rx="2" style="fill:var(--warn)"/><text x="40.0" y="340.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">改了已有的代码</text><rect x="170.0" y="330.0" width="14.0" height="12.0" rx="2" style="fill:var(--accent)"/><text x="190.0" y="340.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">新增的代码</text><text x="400.0" y="340.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">main() 是调用方，两边都要把新名字加进列表</text></g></svg>
</figure>

问题不在代码长短，而在**修改分散**（违反开闭原则）：`switch` 版一共要动 **7 处**，`parseAlgo()` 里漏加一个 `if` 编译器一声不吭，用户选 `lanczos` 会悄悄退回双线性；而且所有算法挤在同一个函数里，改一次枚举，所有包含它的文件都要重编。

## 2. 策略模式

### 2.1 意图与角色

> **GoF**：定义一系列算法，把它们一个个封装起来，并且使它们可以相互替换。本模式使得算法可以独立于使用它的客户而变化。

大白话：**同一件事有多种做法时，把每种做法封装成一个对象，统一实现同一个接口；调用方只拿着接口，具体用哪种可以随时替换。**

三个角色：
- **Strategy（策略接口）**：所有算法的公共接口（`Scaler`）；
- **ConcreteStrategy（具体策略）**：每种算法一个类（`Nearest`、`Bilinear`、`Bicubic`）；
- **Context（上下文）**：持有策略接口并委托它干活的一方（`Thumbnailer`、`Player`）。

### 2.2 用策略重构

把原来散在 4 个函数里的逻辑按算法收拢成类，再用一张工厂表把名字映射到策略，上下文 `Thumbnailer` 只依赖 `Scaler` 接口：

```cpp
// 《策略模式》：每种缩放算法封装成一个 Scaler 类，Thumbnailer 只依赖接口
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class Scaler {                                    // 策略接口
public:
    virtual ~Scaler() = default;
    virtual const char* name() const = 0;
    virtual int taps() const = 0;
    virtual std::vector<int> scale(const std::vector<int>& row, int outW) const = 0;
};

template <class Kernel>
std::vector<int> resample(const std::vector<int>& row, int outW, Kernel kernel) {
    auto at = [&](int i) { return row[std::clamp(i, 0, static_cast<int>(row.size()) - 1)]; };
    std::vector<int> dst(outW);
    for (int x = 0; x < outW; ++x)
        dst[x] = kernel(at, (x + 0.5f) * row.size() / outW - 0.5f);
    return dst;
}

class Nearest : public Scaler {
public:
    const char* name() const override { return "nearest"; }
    int taps() const override { return 1; }
    std::vector<int> scale(const std::vector<int>& row, int w) const override {
        return resample(row, w, [](auto at, float x) { return at(static_cast<int>(std::floor(x + 0.5f))); });
    }
};

class Bilinear : public Scaler {
public:
    const char* name() const override { return "bilinear"; }
    int taps() const override { return 4; }
    std::vector<int> scale(const std::vector<int>& row, int w) const override {
        return resample(row, w, [](auto at, float x) {
            int x0 = static_cast<int>(std::floor(x));
            float fx = x - x0;
            return static_cast<int>(at(x0) * (1 - fx) + at(x0 + 1) * fx + 0.5f);
        });
    }
};

class Bicubic : public Scaler {
public:
    const char* name() const override { return "bicubic"; }
    int taps() const override { return 16; }
    std::vector<int> scale(const std::vector<int>& row, int w) const override {
        return resample(row, w, [](auto at, float x) {
            int x0 = static_cast<int>(std::floor(x));
            float fx = x - x0, sum = 0;
            for (int i = -1; i <= 2; ++i) sum += at(x0 + i) * cubic(fx - i);
            return std::clamp(static_cast<int>(sum + 0.5f), 0, 255);
        });
    }
private:
    static float cubic(float t) {
        t = std::fabs(t);
        return t < 1 ? (1.5f * t - 2.5f) * t * t + 1
             : t < 2 ? ((-0.5f * t + 2.5f) * t - 4) * t + 2 : 0.f;
    }
};

std::unique_ptr<Scaler> makeScaler(const std::string& name) {   // 策略注册表：替代 if-else
    static const std::map<std::string, std::function<std::unique_ptr<Scaler>()>> table = {
        {"nearest", [] { return std::make_unique<Nearest>(); }},
        {"bilinear", [] { return std::make_unique<Bilinear>(); }},
        {"bicubic", [] { return std::make_unique<Bicubic>(); }},
    };
    auto it = table.find(name);
    return it != table.end() ? it->second() : std::make_unique<Bilinear>();
}

class Thumbnailer {                               // 上下文：只认识 Scaler 接口
public:
    Thumbnailer(int w, std::unique_ptr<Scaler> s) : w_(w), scaler_(std::move(s)) {}
    void setScaler(std::unique_ptr<Scaler> s) { scaler_ = std::move(s); }
    const Scaler& scaler() const { return *scaler_; }
    std::vector<int> make(const std::vector<int>& row) const { return scaler_->scale(row, w_); }
private:
    int w_;
    std::unique_ptr<Scaler> scaler_;
};

int main() {
    std::vector<int> row = {20, 30, 40, 50, 240, 70, 80, 90};
    Thumbnailer thumb(12, makeScaler("nearest"));
    for (std::string arg : {"nearest", "bilinear", "bicubic"}) {
        thumb.setScaler(makeScaler(arg));         // 运行时切换算法
        auto big = thumb.make(row);
        std::printf("%-8s taps=%-2d 结果:", thumb.scaler().name(), thumb.scaler().taps());
        for (int v : big) std::printf(" %3d", v);
        std::printf("\n");
    }
}
```

输出与 `switch` 版完全一致。现在加一种 `Lanczos`，只需新写一个 `class Lanczos : public Scaler` 并在 `makeScaler()` 表里加一行，所有已有算法类和 `Thumbnailer` 一行不动。

### 2.3 类图和时序图

```uml
label: 策略模式：Thumbnailer 持有一个 Scaler；Nearest、Bilinear、Bicubic、Lanczos 都实现 Scaler 接口，新加的 Lanczos 不影响已有的类
classes:
  - { id: Thumb, name: Thumbnailer, x: 20, y: 20, w: 250, attrs: ['- w_, h_ : int', '- scaler_ : unique_ptr<Scaler>'], ops: ['+ setScaler(s)', '+ make(frame) : Plane'] }
  - { id: Scaler, stereo: interface, x: 430, y: 25.5, w: 260, ops: ['*+ name() : const char*', '*+ taps() : int', '*+ scale(src, w, h) : Plane'] }
  - { id: Nearest, x: 250, y: 230, w: 120, ops: ['+ scale(...)'] }
  - { id: Bilinear, x: 390, y: 230, w: 120, ops: ['+ scale(...)'] }
  - { id: Bicubic, x: 530, y: 230, w: 120, ops: ['+ scale(...)'] }
  - { id: Lanczos, x: 670, y: 230, w: 120, ops: ['+ scale(...)'], hl: new }
rels:
  - { from: Thumb, to: Scaler, type: compose, tm: '1' }
  - { from: Nearest, to: Scaler, type: realize, mid: 190, tdx: -90 }
  - { from: Bilinear, to: Scaler, type: realize, mid: 190, tdx: -30 }
  - { from: Bicubic, to: Scaler, type: realize, mid: 190, tdx: 30 }
  - { from: Lanczos, to: Scaler, type: realize, mid: 190, tdx: 90 }
notes:
  - { x: 20, y: 300, w: 330, text: ['make(frame) {', '  return scaler_->scale(frame, w_, h_);', '}'], to: Thumb }
```

```uml
label: 时序图：Thumbnailer 先用 Nearest 生成缩略图；setScaler 换成 Bicubic 以后，同一个 make 调用转给了 Bicubic
participants:
  - { id: m, name: 'main', x: 20, w: 90 }
  - { id: t, name: 'Thumbnailer', x: 190, w: 130 }
  - { id: n, name: 'Nearest', x: 400, w: 110 }
  - { id: c, name: 'Bicubic', x: 590, w: 110 }
messages:
  - { from: m, to: t, text: 'make(frame)', n: 1 }
  - { from: t, to: n, text: 'scale(frame, w, h)' }
  - { from: n, to: t, text: 'Plane', type: reply }
  - { from: t, to: m, text: '', type: reply }
  - { from: m, to: c, text: 'make_unique<Bicubic>()', type: create }
  - { from: m, to: t, text: 'setScaler(bicubic)', n: 2, note: '旧的 Nearest 被释放' }
  - { from: m, to: t, text: 'make(frame)', n: 3 }
  - { from: t, to: c, text: 'scale(frame, w, h)' }
  - { from: c, to: t, text: 'Plane', type: reply }
  - { from: t, to: m, text: '', type: reply }
```

### 2.4 为什么要在运行时换：实测速度与画质

策略模式的价值不只是「去掉 switch」，更在于**没有哪一种算法在所有场合都最优**。用真实 2D 缩放程序（`projects/dp-lab/18-strategy/quality.cpp`）把 `bbb-10s.mp4` 第 5 秒的 720p 画面放大到 1080p（朴素 C++ 实现，无 SIMD，`-O2`）：

<figure class="diagram">
<svg viewBox="0 0 720 186" role="img" aria-label="四种缩放策略把一帧 720p 放大到 1080p 的耗时。Clang：nearest 0.97、bilinear 2.65、bicubic 20.40、lanczos 93.34 毫秒；GCC：1.47、2.77、20.84、112.53 毫秒"><g style="font-family:var(--mono)"><text x="178.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang 21 / macOS</text><rect x="178.0" y="30.0" width="1.5" height="20.0" rx="2" style="fill:var(--warn)"/><text x="185.5" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">0.97 ms</text><rect x="178.0" y="60.0" width="3.8" height="20.0" rx="2" style="fill:var(--accent)"/><text x="187.8" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">2.65 ms</text><rect x="178.0" y="90.0" width="29.4" height="20.0" rx="2" style="fill:var(--link)"/><text x="213.4" y="105.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">20.40 ms</text><rect x="178.0" y="120.0" width="134.4" height="20.0" rx="2" style="fill:var(--danger)"/><text x="318.4" y="135.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">93.34 ms</text><text x="448.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC 13 / Ubuntu</text><rect x="448.0" y="30.0" width="2.1" height="20.0" rx="2" style="fill:var(--warn)"/><text x="456.1" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">1.47 ms</text><rect x="448.0" y="60.0" width="4.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="458.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">2.77 ms</text><rect x="448.0" y="90.0" width="30.0" height="20.0" rx="2" style="fill:var(--link)"/><text x="484.0" y="105.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">20.84 ms</text><rect x="448.0" y="120.0" width="162.0" height="20.0" rx="2" style="fill:var(--danger)"/><text x="616.0" y="135.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">112.53 ms</text><text x="170.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">nearest</text><text x="170.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">bilinear</text><text x="170.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">bicubic</text><text x="170.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">lanczos</text><text x="10.0" y="178.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">quality.cpp：bbb-10s.mp4 第 5 秒灰度帧，720p → 1080p，-O2，7 次取中位数</text></g></svg>
</figure>

| 策略 | Clang (720p→1080p) | GCC (720p→1080p) | 缩小再放大 PSNR | 适用场合 |
| --- | ---: | ---: | ---: | --- |
| nearest | 0.97 ms | 1.47 ms | 41.99 dB | 缩略图墙、极端性能受限 |
| bilinear | 2.65 ms | 2.77 ms | 47.96 dB | 实时播放默认、掉帧降级 |
| bicubic | 20.40 ms | 20.84 ms | 49.83 dB | 高画质实时播放 |
| lanczos | 93.34 ms | 112.53 ms | 50.06 dB | 离线截图、导出封面 |

<figure>
<img src="/img/dp/strategy-eye.jpg" alt="大兔子眼睛附近的一小块放大 6 倍：最近邻是一个个方块，双线性平滑但模糊，双三次和 lanczos 的眼眶边缘更清楚，两者几乎看不出差别" width="680">
<figcaption>左起：nearest、bilinear、bicubic、lanczos（眼睛局部放大 6 倍）</figcaption>
</figure>

### 2.5 运行时切换：掉帧时自动降级

30 fps 视频每帧预算 33.3 ms。用户选了 `bicubic`（~21 ms），碰到复杂画面解码耗时飙到 15 ms 就会超时。播放器可以在运行时临时降级到 `bilinear`（~3 ms），缓过来再升回去：

```cpp
// 《策略模式》：运行时切换策略——连续超时从 bicubic 降到 bilinear，缓过来再升回去
#include <cstdio>
#include <memory>
#include <vector>

class Scaler {
public:
    virtual ~Scaler() = default;
    virtual const char* name() const = 0;
    virtual double costMs() const = 0;            // 模拟各策略在 720p→1080p 下的实测耗时量级
};
struct Nearest  : Scaler { const char* name() const override { return "nearest"; }  double costMs() const override { return 1; } };
struct Bilinear : Scaler { const char* name() const override { return "bilinear"; } double costMs() const override { return 3; } };
struct Bicubic  : Scaler { const char* name() const override { return "bicubic"; }  double costMs() const override { return 21; } };

class Player {
public:
    explicit Player(double budgetMs) : budget_(budgetMs) {
        ladder_.push_back(std::make_unique<Nearest>());
        ladder_.push_back(std::make_unique<Bilinear>());
        ladder_.push_back(std::make_unique<Bicubic>());
    }
    void prefer(int level) { preferred_ = cur_ = level; }

    void playFrame(int n, double decodeMs) {
        double total = decodeMs + ladder_[cur_]->costMs();
        std::printf("帧 %2d  解码 %4.1f + %-8s → %5.1f ms", n, decodeMs, ladder_[cur_]->name(), total);
        if (total > budget_) {
            calm_ = 0;
            std::printf("  超时");
            if (++late_ >= 2 && cur_ > 0) { --cur_; late_ = 0; std::printf("  → 降级为 %s", ladder_[cur_]->name()); }
        } else {
            late_ = 0;
            bool upFits = cur_ < preferred_ && decodeMs + ladder_[cur_ + 1]->costMs() < budget_ * 0.9;
            calm_ = upFits ? calm_ + 1 : 0;
            if (calm_ >= 3) { ++cur_; calm_ = 0; std::printf("  → 恢复为 %s", ladder_[cur_]->name()); }
        }
        std::printf("\n");
    }
private:
    double budget_;
    std::vector<std::unique_ptr<Scaler>> ladder_;
    int cur_ = 1, preferred_ = 1, late_ = 0, calm_ = 0;
};

int main() {
    Player player(1000.0 / 30);                   // 30 fps：每帧 33.3 ms
    player.prefer(2);                             // 用户选了高画质 bicubic
    double decode[] = {6, 7, 15, 16, 15, 14, 6, 6, 7, 6};
    for (int i = 0; i < 10; ++i) player.playFrame(i + 1, decode[i]);
}
```

```text
帧  1  解码  6.0 + bicubic  →  27.0 ms
帧  2  解码  7.0 + bicubic  →  28.0 ms
帧  3  解码 15.0 + bicubic  →  36.0 ms  超时
帧  4  解码 16.0 + bicubic  →  37.0 ms  超时  → 降级为 bilinear
帧  5  解码 15.0 + bilinear →  18.0 ms
帧  6  解码 14.0 + bilinear →  17.0 ms
帧  7  解码  6.0 + bilinear →   9.0 ms
帧  8  解码  6.0 + bilinear →   9.0 ms
帧  9  解码  7.0 + bilinear →  10.0 ms  → 恢复为 bicubic
帧 10  解码  6.0 + bicubic  →  27.0 ms
```

下面的演示把「换算法」和「掉帧降级」放在一起，点按钮看每一步：

<div class="dsviz" data-viz="dp-strategy"></div>

注意：**选择策略的逻辑在上下文 `Player` 里**，各个策略类互不知道对方。这是策略和状态模式最核心的分界线。

## 3. 深入策略模式

### 3.1 标准库与真实工程里的策略

- **C++ 标准库（编译期策略 / Policy）**：`std::sort` 的比较器、`std::unique_ptr<T, Deleter>` 的删除器、`std::unordered_map` 的 `Hash / KeyEqual` 都是通过**模板参数**注入的策略。编译期定死、不能运行时换，但零间接调用、可完全内联，借助空基类优化（EBO）连内存都不多占（`sizeof(unique_ptr<T, EmptyDeleter>) == 8`）。
- **FFmpeg（C 语言函数指针表）**：每个解码器是一个 `FFCodec` 结构体，填上自己的 `init / decode / close` 函数指针；上层只调用 `avcodec_send_packet / receive_frame`，H.264 / HEVC / AV1 可以无缝替换。反过来，libswscale 的 `SWS_BICUBIC` 是用标志位分支生成滤波系数，因为缩放算法集合封闭、由库内部掌控。

### 3.2 有状态的策略不能共用

没有数据成员的策略（如 `Nearest`、`Bilinear`）可以在多个上下文间共用甚至做成静态单例；但**带状态的策略**（如码率控制里的 CBR 缓冲区、缓存列权重表的优化版双线性）**绝对不能共用**：

- 两个不同分辨率的缩略图生成器共用同一个带缓存的 `Bilinear`，第二个会误用第一个算出来的列权重表，输出错图；
- 多线程下共用还会导致数据竞争。

原则：无状态策略可以把 `scale()` 标成 `const` 并共享；有状态策略用 `unique_ptr` 独占，或由工厂每次创建新实例。

### 3.3 什么时候不用策略

- 只有两三种分支且几乎不会新增：直接写 `if-else` 更清晰；
- 算法集合完全封闭、由你自己掌控：用 `enum + switch` 或 `std::variant`，漏分支时编译器能直接提醒；
- 每种算法需要的参数截然不同：强行塞进一个宽接口只会让大部分实现类忽略一半参数。

## 4. 现代 C++ 写法与性能

### 4.1 四种写法对比

在 C++ 里，策略不必总是写成「抽象基类 + 派生类」。下面用同一个上下文对比虚函数、`std::function`、模板参数、`std::variant` 四种写法：

```cpp
// 《策略模式》：现代 C++ 四种策略写法——虚函数、std::function、模板参数、std::variant
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <variant>
#include <vector>

template <class Kernel>
std::vector<int> resample(const std::vector<int>& row, int outW, Kernel k) {
    auto at = [&](int i) { return row[std::clamp(i, 0, static_cast<int>(row.size()) - 1)]; };
    std::vector<int> dst(outW);
    for (int x = 0; x < outW; ++x) dst[x] = k(at, (x + 0.5f) * row.size() / outW - 0.5f);
    return dst;
}

// 两个算法写成普通函数对象，四种写法共用
struct NearestK {
    template <class At> int operator()(At at, float x) const { return at(static_cast<int>(std::floor(x + 0.5f))); }
};
struct BilinearK {
    template <class At> int operator()(At at, float x) const {
        int x0 = static_cast<int>(std::floor(x));
        float fx = x - x0;
        return static_cast<int>(at(x0) * (1 - fx) + at(x0 + 1) * fx + 0.5f);
    }
};

// ① 虚函数：支持多方法、可插件扩展
struct Scaler { virtual ~Scaler() = default; virtual std::vector<int> scale(const std::vector<int>&, int) const = 0; };
template <class K> struct KernelScaler : Scaler {
    std::vector<int> scale(const std::vector<int>& r, int w) const override { return resample(r, w, K{}); }
};
struct ThumbV {
    std::unique_ptr<Scaler> s;
    std::vector<int> make(const std::vector<int>& r) const { return s->scale(r, 4); }
};

// ② std::function：单函数策略最省事，lambda 还能顺手捕获参数
using ScaleFn = std::function<std::vector<int>(const std::vector<int>&, int)>;
struct ThumbF {
    ScaleFn f;
    std::vector<int> make(const std::vector<int>& r) const { return f(r, 4); }
};

// ③ 模板参数：编译期选定，可完全内联
template <class K = BilinearK>
struct ThumbT {
    std::vector<int> make(const std::vector<int>& r) const { return resample(r, 4, K{}); }
};

// ④ std::variant：集合封闭、按值存放、零堆分配
struct ThumbVar {
    std::variant<NearestK, BilinearK> k;
    std::vector<int> make(const std::vector<int>& r) const {
        return std::visit([&](auto algo) { return resample(r, 4, algo); }, k);
    }
};

void show(const char* tag, const std::vector<int>& v) {
    std::printf("%-20s", tag);
    for (int x : v) std::printf(" %3d", x);
    std::printf("\n");
}

int main() {
    std::vector<int> row = {20, 30, 40, 50, 240, 70, 80, 90};

    ThumbV v{std::make_unique<KernelScaler<NearestK>>()};
    show("① 虚函数 nearest", v.make(row));

    float gain = 1.2f;                            // lambda 顺手带参数：缩放并提亮 20%
    ThumbF f{[gain](const std::vector<int>& r, int w) {
        return resample(r, w, [gain](auto at, float x) { return std::clamp(static_cast<int>(BilinearK{}(at, x) * gain), 0, 255); });
    }};
    show("② function 提亮", f.make(row));

    show("③ 模板 bilinear", ThumbT<>{}.make(row));

    ThumbVar var{NearestK{}};
    var.k = BilinearK{};                          // 运行时切换：直接赋新值
    show("④ variant bilinear", var.make(row));
}
```

```text
① 虚函数 nearest       30  50  70  90
② function 提亮        30  54 186 102
③ 模板 bilinear        25  45 155  85
④ variant bilinear     25  45 155  85
```

### 4.2 开销实测：粒度比写法重要

很多文章纠结「虚函数和 `std::function` 谁慢」，其实真正决定开销的是**策略调用的粒度**。把 720p 放大到 1080p（207 万个输出像素），分别测「每像素调用一次策略」和「每帧调用一次策略」（完整压测代码见 `projects/dp-lab/18-strategy/bench_granularity.cpp` 和 `sizes.cpp`）：

<figure class="diagram">
<svg viewBox="0 0 750 184" role="img" aria-label="策略的粒度：细粒度时 resample 循环对 1080p 的每个像素调用一次 Kernel::sample，共 2,073,600 次间接调用，核函数无法内联；粗粒度时 Player 每帧只调用一次 Bilinear::scale，循环在策略内部用模板实现，全部内联"><g style="font-family:var(--mono)"><text x="20.0" y="20.0" text-anchor="start" style="fill:var(--fg);font-size:12.5px;font-weight:700">细粒度：策略是「取一个像素」</text><rect x="20" y="36" width="150" height="40" rx="4" style="fill:none;stroke:var(--border);stroke-width:1.3"/><text x="95.0" y="60.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">resample 循环</text><line x1="170" y1="56" x2="373" y2="56" style="stroke:var(--warn);stroke-width:1.6"/><path d="M380,56 L371,51 L371,61 z" style="fill:var(--warn)"/><text x="275.0" y="49.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">sample(x, y) × 2,073,600 次</text><rect x="380" y="36" width="160" height="40" rx="4" style="fill:none;stroke:var(--border);stroke-width:1.3"/><text x="460.0" y="60.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">Kernel::sample()</text><text x="560.0" y="52.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">每次都是一次间接调用，</text><text x="560.0" y="68.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">核函数没法内联进循环</text><text x="20.0" y="112.0" text-anchor="start" style="fill:var(--fg);font-size:12.5px;font-weight:700">粗粒度：策略是「缩放一整帧」</text><rect x="20" y="128" width="150" height="40" rx="4" style="fill:none;stroke:var(--border);stroke-width:1.3"/><text x="95.0" y="152.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">Player</text><line x1="170" y1="148" x2="373" y2="148" style="stroke:var(--accent);stroke-width:1.6"/><path d="M380,148 L371,143 L371,153 z" style="fill:var(--accent)"/><text x="275.0" y="141.0" text-anchor="middle" style="fill:var(--accent);font-size:11.5px;font-weight:700">scale(frame) × 1 次</text><rect x="380" y="128" width="160" height="40" rx="4" style="fill:none;stroke:var(--border);stroke-width:1.3"/><text x="460.0" y="152.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">Bilinear::scale()</text><text x="560.0" y="144.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">虚调用只有一次，</text><text x="560.0" y="160.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">里面的循环用模板，全部内联</text></g></svg>
</figure>

| 双线性 720p→1080p（ms/帧，`-O2` 7 轮中位数） | Clang / macOS | GCC 13 / Ubuntu | 说明 |
| --- | ---: | ---: | --- |
| 虚函数（每像素调 207 万次） | 3.55 | 3.61 | 无法内联，比模板慢 18%～28% |
| `std::function`（每像素） | 3.90 | 3.92 | 同上，且捕获超 16 字节会堆分配 |
| `variant + visit`（每像素） | 4.08 | 3.63 | 每次像素循环都要分支分派 |
| 模板参数 + 禁止内联 | 3.56 | 3.64 | 证明**贵的不是「虚」，而是「不能内联」** |
| 模板参数（默认内联） | 3.00 | 2.82 | 核函数完全内联进循环 |
| **虚函数（每帧只调 1 次）** | **2.96** | **2.77** | **和模板一样快，且保留运行时切换** |

结论非常干脆：设计策略接口时，把热循环留在策略内部（如按「整帧」而不是按「单像素」调用），虚函数就能既保留运行时灵活性，又拿到和模板一样的满速性能。

### 4.3 怎么选

| 写法 | 运行时换 | 多方法 | 开放扩展 | 适合场景 |
| --- | :---: | :---: | :---: | --- |
| 虚函数接口 | ✓ | ✓ | ✓（插件） | 粗粒度策略、策略有多个方法、需要动态扩展 |
| `std::function` | ✓ | ✗ | ✓ | 只有一个函数的策略、需要 lambda 捕获少量参数（≤ 16 B 走 SBO 零分配） |
| 模板参数 | ✗ | ✓ | ✓（编译期） | 细粒度热循环、标准库式 policy（如 `unique_ptr` 删除器） |
| `std::variant` | ✓ | ✓ | ✗（封闭） | 算法种类固定、希望按值连续存放、零堆分配 |

## 5. 策略和相似的模式

| 模式 | 核心关注点 | 和策略的区别 |
| --- | --- | --- |
| **状态** | 对象在不同状态下行为不同 | 类图几乎相同，但**策略由外部选、策略之间互不知道**；**状态自己决定下一个状态**（如 `Playing::pause()` 切到 `Paused`） |
| **模板方法** | 流程固定，个别步骤可变 | 模板方法用**继承**在编译期替换步骤；策略用**组合**在运行时替换整个算法 |
| **桥接** | 两个独立变化的维度 | 策略通常只有上下文一个类；桥接两边都是继承体系 |
| **命令** | 把一次请求封装成对象 | 策略回答「这件事**怎么**做」（可互换）；命令回答「**要做**哪件事」（可排队、撤销） |

## 6. 小结

- 策略模式把一族可互换的算法封装成类，上下文只依赖接口；加新算法只需新增类并登记，不改已有逻辑。
- 没有哪种算法全面占优（速度与画质往往互相妥协），策略让系统能在运行时按需切换。
- **粒度决定性能**：热循环放在策略内部、按粗粒度调用时，虚函数和模板一样快；需要细粒度内联时用模板参数；单函数策略优先 `std::function`。
- 无状态策略可共享；有状态策略（如带缓存的缩放器）必须每个上下文独占一份。

想动手练一把？刷题里的重构挑战 [运费计算](/problems/refactor-shipping-strategy/) 就是把按快递方式 `if-else` 的代码重构成策略模式，提交时会同时检查用例和代码结构。

## 7. 练习

1. **辨析**：下面四个设计分别属于策略、状态、模板方法还是桥接？
   - (a) 播放列表有「顺序 / 单曲循环 / 随机」三种 `NextPolicy`，用户点按钮切换；
   - (b) 下载任务有「等待 / 下载中 / 暂停 / 完成」，`Downloading::onFinished()` 自动把任务切到 `Done`；
   - (c) `Decoder::run()` 固定执行「打开 → 循环解码 → 冲刷 → 关闭」，子类只重写「解码一个包」；
   - (d) 播放器有主窗口、画中画两种视图，又有 OpenGL、Metal 两种渲染后端，各自成继承体系。
2. **设计**：视频编码器的码率控制有 CQP（固定 QP）、CBR（恒定码率）、CRF（恒定质量）三种。编码每帧前要决定量化参数 QP，编码后知道这一帧实际花了多少比特。你会怎么设计 `RateControl` 策略接口？哪一种策略是有状态的、不能在两个编码器之间共享？
3. **找问题**：下面两个缩略图生成器为了「省内存」，共用了一个带列坐标缓存的 `CachedBilinear` 策略对象。结果高清视频的缩略图明显偏暗出错，问题出在哪？怎么改？

```cpp
// 《策略模式》练习 3：两个 Thumbnailer 共用带缓存的策略对象，高清图算错了
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

class CachedBilinear {                            // 缓存输出每一列对应的源坐标，避免重复计算
public:
    std::vector<int> scale(const std::vector<int>& src, int outW) const {
        if (static_cast<int>(col_.size()) != outW) {   // 只检查了输出宽度 outW，没检查输入宽度 src.size()！
            col_.resize(outW);
            for (int x = 0; x < outW; ++x) col_[x] = (x + 0.5f) * src.size() / outW - 0.5f;
        }
        std::vector<int> dst(outW);
        for (int x = 0; x < outW; ++x) {
            int i = std::clamp(static_cast<int>(std::floor(col_[x])), 0, static_cast<int>(src.size()) - 2);
            float f = col_[x] - i;
            dst[x] = static_cast<int>(src[i] * (1 - f) + src[i + 1] * f + 0.5f);
        }
        return dst;
    }
private:
    mutable std::vector<float> col_;
};

int main() {
    auto shared = std::make_shared<CachedBilinear>();
    std::vector<int> sd = {0, 31, 62, 94, 125, 157, 188, 220};                      // 标清 8 像素渐变
    std::vector<int> hd = {0, 14, 29, 44, 58, 73, 88, 102, 117, 132, 146, 161, 176, 190, 205, 220}; // 高清 16 像素渐变
    auto print = [](const char* tag, const std::vector<int>& v) {
        std::printf("%s", tag);
        for (int x : v) std::printf(" %3d", x);
        std::printf("\n");
    };
    print("标清 8  → 4：", shared->scale(sd, 4));
    print("高清 16 → 4：", shared->scale(hd, 4));
    print("高清(新对象)：", CachedBilinear{}.scale(hd, 4));
}
```

<details>
<summary>点击查看答案</summary>

1. (a) **策略**：三种模式平等，由外部切换，互不知道对方。
   (b) **状态**：`Downloading` 内部自己决定切到 `Done`。
   (c) **模板方法**：骨架写死在基类，子类通过继承重写单步。
   (d) **桥接**：视图 × 渲染后端两个独立变化的维度。

2. 接口只需两个函数：`virtual int nextQp(double complexity) = 0;`（编码前给出 QP）和 `virtual void feedback(int actualBits) {}`（编码后反馈实际比特数，默认空实现，因为 CQP/CRF 不需要它）。**CBR 是有状态的**（必须维护码率缓冲区/累计欠账），每个编码器实例必须独占一份，不能共享。完整模拟程序见 `projects/dp-lab/18-strategy/ex3_rate_control.cpp`。

3. 有两个问题：
   - 缓存的键不完整：`col_` 既依赖输出宽度 `outW`，也依赖输入宽度 `src.size()`。高清图调用时 `outW` 同样是 4，直接复用了标清图 `0..7` 的采样坐标，只采了高清图左半边，画面全变暗；
   - 根源上，**有缓存的策略是有状态的，不应该在多个上下文间共用**（多线程下还会数据竞争）。

   改法：把缓存键补全为 `(srcW_, outW_)`，更重要的是让每个 `Thumbnailer` 用 `std::unique_ptr<Scaler>` **独占**自己的策略对象（完整 2D 修复版见 `projects/dp-lab/18-strategy/ex4_fixed.cpp`）。

</details>
