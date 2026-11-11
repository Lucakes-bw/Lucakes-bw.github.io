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
description: 播放器要支持最近邻、双线性、双三次三种缩放算法，用 enum + switch 写，算法相关的代码散在 4 个函数里，加一种 Lanczos 要改 7 处；改成策略模式后，新增一个类、在表里登记一行，已有的类一个都不动。用 bbb-10s.mp4 的真实画面实测四种策略：720p 放大到 1080p 每帧 0.97～93.34 ms，画质 41.99～50.06 dB，没有哪一种全面占优，所以要能在运行时换（掉帧时自动降级）。再量现代 C++ 的四种写法：每个像素调用一次策略时，虚函数、std::function、variant 都比模板参数慢 18%～39%，原因是核函数没法内联；把策略的粒度放大到「整帧」，虚函数每帧只调一次，就和模板一样快。最后讲 unique_ptr 删除器这样的编译期策略、有状态的策略不能共用，以及策略和状态、模板方法、桥接的区别。
top_group_list: true
disableNunjucks: true
viz: [dp-strategy]
---

> **本文目标**：理解策略模式：把一族可以互换的算法分别封装成对象，上下文只依赖它们的公共接口，在运行时选择或替换；会把 `enum` + `switch` 重构成策略；量出加一种算法要改几处、四种算法的速度和画质；会用虚函数、`std::function`、模板参数、`std::variant` 四种方式写策略，知道调用开销取决于策略的**粒度**；分清策略和状态、模板方法、桥接。
>
> **前置知识**：上一篇《代理模式》；《为什么需要设计模式》（那里第一次见到策略）、《SOLID 原则（上）：单一职责与开闭》（密码规则的 `std::function` 列表其实就是策略）、《组合优于继承与 C++ 的四种多态》（四种写法的一般比较）、《桥接模式》；音视频专栏的《像素格式转换与重采样》（缩放算法的原理）。这是第 4 章「行为型模式」的第一篇：创建型模式关心对象怎么创建，结构型模式关心对象怎么组合，**行为型模式关心对象之间怎么分配职责、怎么协作**。
>
> **编译环境**：g++/clang++ -std=c++17，完整代码在 projects/dp-lab/18-strategy/

## 1. 问题：缩放算法越加越多

### 1.1 一个按 enum 分支的缩放器

播放器要把解码出来的帧缩放到窗口大小：生成缩略图墙时要快，全屏播放时要清楚，所以支持了三种算法（原理见音视频专栏《像素格式转换与重采样》）：

- **最近邻**（nearest）：取离采样点最近的 1 个源像素；
- **双线性**（bilinear）：取周围 2×2 = 4 个像素，按距离加权平均；
- **双三次**（bicubic）：取周围 4×4 = 16 个像素，用三次卷积核加权。

最直接的写法是一个 `enum`，哪里和算法有关，哪里就 `switch` 一下。下面的程序把一块 8×4 的测试图（中间有一条竖直的亮线）放大 1.5 倍：

```cpp
// 《策略模式》：问题版——用 enum + switch 选缩放算法，和算法有关的代码散落在 4 个函数里
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

struct Plane {                                   // 一个 8 位灰度平面（视频帧的 Y 分量）
    int w, h;
    std::vector<uint8_t> px;
    Plane(int w_, int h_) : w(w_), h(h_), px(static_cast<size_t>(w_) * h_) {}
    int at(int x, int y) const {                 // 越界时取边上的像素
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return px[static_cast<size_t>(y) * w + x];
    }
};

enum class Algo { Nearest, Bilinear, Bicubic };

// ① 命令行参数 → 算法
Algo parseAlgo(const std::string& s) {
    if (s == "nearest") return Algo::Nearest;
    if (s == "bilinear") return Algo::Bilinear;
    if (s == "bicubic") return Algo::Bicubic;
    return Algo::Bilinear;
}

// ② 算法 → 名字（日志、界面上的下拉框）
const char* algoName(Algo a) {
    switch (a) {
    case Algo::Nearest: return "nearest";
    case Algo::Bilinear: return "bilinear";
    case Algo::Bicubic: return "bicubic";
    }
    return "?";
}

// ③ 每个输出像素要读几个源像素（估算开销用）
int algoTaps(Algo a) {
    switch (a) {
    case Algo::Nearest: return 1;
    case Algo::Bilinear: return 4;
    case Algo::Bicubic: return 16;
    }
    return 0;
}

float cubic(float t) {                           // Keys 三次卷积核，a = -0.5
    t = std::fabs(t);
    if (t < 1) return (1.5f * t - 2.5f) * t * t + 1;
    if (t < 2) return ((-0.5f * t + 2.5f) * t - 4) * t + 2;
    return 0;
}

// ④ 在源图的 (x, y) 处取一个值：每个算法的核心都挤在这一个 switch 里
float sample(const Plane& p, float x, float y, Algo a) {
    int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    float fx = x - x0, fy = y - y0;
    switch (a) {
    case Algo::Nearest:
        return p.at(static_cast<int>(std::floor(x + 0.5f)), static_cast<int>(std::floor(y + 0.5f)));
    case Algo::Bilinear: {
        float top = p.at(x0, y0) * (1 - fx) + p.at(x0 + 1, y0) * fx;
        float bot = p.at(x0, y0 + 1) * (1 - fx) + p.at(x0 + 1, y0 + 1) * fx;
        return top * (1 - fy) + bot * fy;
    }
    case Algo::Bicubic: {
        float wx[4], wy[4], sum = 0;                // 横竖各 4 个权重，先算好
        for (int i = 0; i < 4; ++i) wx[i] = cubic(fx - (i - 1)), wy[i] = cubic(fy - (i - 1));
        for (int j = 0; j < 4; ++j)
            for (int i = 0; i < 4; ++i) sum += p.at(x0 + i - 1, y0 + j - 1) * wx[i] * wy[j];
        return sum;
    }
    }
    return 0;
}

Plane scale(const Plane& src, int w, int h, Algo a) {
    Plane dst(w, h);
    float rx = static_cast<float>(src.w) / w, ry = static_cast<float>(src.h) / h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float v = sample(src, (x + 0.5f) * rx - 0.5f, (y + 0.5f) * ry - 0.5f, a);
            dst.px[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(std::clamp(v + 0.5f, 0.f, 255.f));
        }
    return dst;
}

int main() {
    Plane frame(8, 4);                           // 一小块测试图：左暗右亮，中间一条竖直的亮线
    for (int y = 0; y < frame.h; ++y)
        for (int x = 0; x < frame.w; ++x)
            frame.px[static_cast<size_t>(y) * frame.w + x] = static_cast<uint8_t>(x == 4 ? 240 : 20 + x * 10);

    for (std::string arg : {"nearest", "bilinear", "bicubic"}) {
        Algo a = parseAlgo(arg);
        Plane big = scale(frame, 12, 6, a);      // 放大 1.5 倍
        std::printf("%-8s taps=%-2d 第一行:", algoName(a), algoTaps(a));
        for (int x = 0; x < big.w; ++x) std::printf(" %3d", big.at(x, 0));
        std::printf("\n");
    }
}
```

```
nearest  taps=1  第一行:  20  30  30  40  50  50 240  70  70  80  90  90
bilinear taps=4  第一行:  20  25  32  38  45  82 208 155  72  78  85  90
bicubic  taps=16 第一行:  19  24  32  38  34  75 227 166  61  76  86  91
```

三种算法对那条亮线（240）的处理不同：最近邻原样复制，边缘生硬；双线性把它摊到两边，变模糊；双三次的边缘最陡，但亮线旁边出现了比原图还暗的 34（原图是 50），这是三次卷积核的负权重造成的「振铃」。没有哪种算法全面占优，所以三种都要留着。

### 1.2 加一种 Lanczos 要改几处

产品又要加一种画质更好的 Lanczos（取周围 6×6 = 36 个像素）。`switch_scaler_lanczos.cpp` 是加完之后的版本，`measure.sh` 用 `diff` 统计改动（去掉第一行注释，相邻的改动算一处）：

<figure class="diagram">
<svg viewBox="0 0 750 354" role="img" aria-label="加一种 Lanczos 算法：switch 版要改 enum、parseAlgo、algoName、algoTaps、sample 和 main 六个已有的地方，再加一个辅助函数；策略版新增一个 Lanczos 类，已有的类不动，只在 makeScaler 的表里登记一行、main 的列表加一项"><g style="font-family:var(--mono)"><text x="20.0" y="20.0" text-anchor="start" style="fill:var(--fg);font-size:12.5px;font-weight:700">switch_scaler.cpp：改 6 个已有的地方</text><text x="400.0" y="20.0" text-anchor="start" style="fill:var(--fg);font-size:12.5px;font-weight:700">strategy_scaler.cpp：已有的类一个都不动</text><rect x="20" y="44" width="330" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="30.0" y="60.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">enum class Algo { …, Lanczos }</text><rect x="20" y="74" width="330" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="30.0" y="90.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">parseAlgo()  +1 个 if</text><rect x="20" y="104" width="330" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="30.0" y="120.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">algoName()  +1 个 case</text><rect x="20" y="134" width="330" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="30.0" y="150.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">algoTaps()  +1 个 case</text><rect x="20" y="164" width="330" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1.3"/><text x="30.0" y="180.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">cubic()</text><rect x="20" y="194" width="330" height="24" rx="3" style="fill:var(--accent);fill-opacity:.16;stroke:var(--accent);stroke-width:1.3"/><text x="30.0" y="210.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">lanczos3()  新的辅助函数</text><rect x="20" y="224" width="330" height="34" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="30.0" y="245.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">sample()  +1 个 case（10 行）</text><rect x="20" y="264" width="330" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1.3"/><text x="30.0" y="280.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">scale()</text><rect x="20" y="294" width="330" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="30.0" y="310.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">main()  命令行列表 +1</text><rect x="400" y="44" width="330" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1.3"/><text x="410.0" y="60.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">class Scaler（接口）</text><rect x="400" y="74" width="330" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1.3"/><text x="410.0" y="90.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">resample()</text><rect x="400" y="104" width="330" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1.3"/><text x="410.0" y="120.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">class Nearest</text><rect x="400" y="134" width="330" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1.3"/><text x="410.0" y="150.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">class Bilinear</text><rect x="400" y="164" width="330" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1.3"/><text x="410.0" y="180.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">class Bicubic</text><rect x="400" y="194" width="330" height="34" rx="3" style="fill:var(--accent);fill-opacity:.16;stroke:var(--accent);stroke-width:1.3"/><text x="410.0" y="215.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">class Lanczos  新增（28 行）</text><rect x="400" y="234" width="330" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="410.0" y="250.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">makeScaler()  表里登记 1 行</text><rect x="400" y="264" width="330" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1.3"/><text x="410.0" y="280.0" text-anchor="start" style="fill:var(--fg);font-size:11.5px">class Thumbnailer（上下文）</text><rect x="400" y="294" width="330" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="410.0" y="310.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">main()  命令行列表 +1</text><rect x="20.0" y="330.0" width="14.0" height="12.0" rx="2" style="fill:var(--warn)"/><text x="40.0" y="340.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">改了已有的代码</text><rect x="170.0" y="330.0" width="14.0" height="12.0" rx="2" style="fill:var(--accent)"/><text x="190.0" y="340.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">新增的代码</text><text x="400.0" y="340.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">main() 是调用方，两边都要把新名字加进列表</text></g></svg>
</figure>

| 加一种 Lanczos | 改动几处 | 删除 / 改写旧代码 | 新增 |
| --- | --- | --- | --- |
| `enum` + `switch` | 7 处 | 2 行 | 22 行 |
| 策略（第 2 节） | 3 处 | 1 行 | 29 行 |

（Clang/macOS 和 GCC/Ubuntu 上的统计结果相同。）

新增的行数差不多，区别在**改在哪里**。`switch` 版要打开 `enum`、`parseAlgo`、`algoName`、`algoTaps`、`sample` 五个已经写好、测过的地方逐个加分支，漏掉任何一个都会出错：

- `switch` 漏了 `case`，`-Wall` 会警告（`-Wswitch`）；但 `parseAlgo` 是一串 `if`，漏了它编译器一声不吭，用户输入 `lanczos` 会悄悄得到双线性。
- 所有算法挤在 `sample()` 一个函数里，每加一种它就长 10 行；想单独测试 Lanczos，也得经过这个 `switch`。
- 缩略图、播放器、截图导出都包含这个 `enum`，改一次，用到它的文件全部要重新编译。

这正是《SOLID 原则（上）：单一职责与开闭》里说的「对修改不关闭」：变化最频繁的东西（算法有几种）没有被隔离出来，散落在各处。

## 2. 策略模式

### 2.1 意图

GoF 的定义：

> 定义一系列算法，把它们一个个封装起来，并且使它们可以相互替换。本模式使得算法可以独立于使用它的客户而变化。

大白话：**同一件事有好几种做法时，把每种做法写成一个类，它们实现同一个接口；用的人只拿着接口，具体用哪一种可以随时换**。

模式里有三个角色：

- **Strategy（策略接口）**：所有算法的公共接口，这里是 `Scaler`；
- **ConcreteStrategy（具体策略）**：一种算法一个类，`Nearest`、`Bilinear`、`Bicubic`；
- **Context（上下文）**：使用算法的一方，持有一个策略对象，把工作交给它，这里是生成缩略图的 `Thumbnailer`（后面还有 `Player`）。

### 2.2 用策略重构

```cpp
// 《策略模式》：把每种缩放算法封装成一个 Scaler 策略，Thumbnailer 只依赖接口
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct Plane {                                   // 一个 8 位灰度平面（视频帧的 Y 分量）
    int w, h;
    std::vector<uint8_t> px;
    Plane(int w_, int h_) : w(w_), h(h_), px(static_cast<size_t>(w_) * h_) {}
    int at(int x, int y) const {                 // 越界时取边上的像素
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return px[static_cast<size_t>(y) * w + x];
    }
};

// 策略接口：一种缩放算法
class Scaler {
public:
    virtual ~Scaler() = default;
    virtual const char* name() const = 0;
    virtual int taps() const = 0;                // 每个输出像素读几个源像素
    virtual Plane scale(const Plane& src, int w, int h) const = 0;
};

// 各个算法共用的外层循环：把输出坐标映射回源图，交给 kernel 取值
template <class Kernel>
Plane resample(const Plane& src, int w, int h, Kernel kernel) {
    Plane dst(w, h);
    float rx = static_cast<float>(src.w) / w, ry = static_cast<float>(src.h) / h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float v = kernel(src, (x + 0.5f) * rx - 0.5f, (y + 0.5f) * ry - 0.5f);
            dst.px[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(std::clamp(v + 0.5f, 0.f, 255.f));
        }
    return dst;
}

class Nearest : public Scaler {
public:
    const char* name() const override { return "nearest"; }
    int taps() const override { return 1; }
    Plane scale(const Plane& src, int w, int h) const override {
        return resample(src, w, h, [](const Plane& p, float x, float y) -> float {
            return p.at(static_cast<int>(std::floor(x + 0.5f)), static_cast<int>(std::floor(y + 0.5f)));
        });
    }
};

class Bilinear : public Scaler {
public:
    const char* name() const override { return "bilinear"; }
    int taps() const override { return 4; }
    Plane scale(const Plane& src, int w, int h) const override {
        return resample(src, w, h, [](const Plane& p, float x, float y) {
            int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
            float fx = x - x0, fy = y - y0;
            float top = p.at(x0, y0) * (1 - fx) + p.at(x0 + 1, y0) * fx;
            float bot = p.at(x0, y0 + 1) * (1 - fx) + p.at(x0 + 1, y0 + 1) * fx;
            return top * (1 - fy) + bot * fy;
        });
    }
};

class Bicubic : public Scaler {
public:
    const char* name() const override { return "bicubic"; }
    int taps() const override { return 16; }
    Plane scale(const Plane& src, int w, int h) const override {
        return resample(src, w, h, [](const Plane& p, float x, float y) {
            int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
            float fx = x - x0, fy = y - y0;
            float wx[4], wy[4], sum = 0;                // 横竖各 4 个权重，先算好
            for (int i = 0; i < 4; ++i) wx[i] = cubic(fx - (i - 1)), wy[i] = cubic(fy - (i - 1));
            for (int j = 0; j < 4; ++j)
                for (int i = 0; i < 4; ++i) sum += p.at(x0 + i - 1, y0 + j - 1) * wx[i] * wy[j];
            return sum;
        });
    }

private:
    static float cubic(float t) {                // Keys 三次卷积核，a = -0.5
        t = std::fabs(t);
        if (t < 1) return (1.5f * t - 2.5f) * t * t + 1;
        if (t < 2) return ((-0.5f * t + 2.5f) * t - 4) * t + 2;
        return 0;
    }
};

// 名字 → 策略：命令行、配置文件、界面下拉框都从这里取
std::unique_ptr<Scaler> makeScaler(const std::string& name) {
    static const std::map<std::string, std::function<std::unique_ptr<Scaler>()>> table = {
        {"nearest", [] { return std::make_unique<Nearest>(); }},
        {"bilinear", [] { return std::make_unique<Bilinear>(); }},
        {"bicubic", [] { return std::make_unique<Bicubic>(); }},
    };
    auto it = table.find(name);
    return it != table.end() ? it->second() : std::make_unique<Bilinear>();
}

// 上下文：生成缩略图。它不知道有哪些算法，只知道 Scaler 接口
class Thumbnailer {
public:
    Thumbnailer(int w, int h, std::unique_ptr<Scaler> s) : w_(w), h_(h), scaler_(std::move(s)) {}
    void setScaler(std::unique_ptr<Scaler> s) { scaler_ = std::move(s); }
    const Scaler& scaler() const { return *scaler_; }
    Plane make(const Plane& frame) const { return scaler_->scale(frame, w_, h_); }

private:
    int w_, h_;
    std::unique_ptr<Scaler> scaler_;
};

int main() {
    Plane frame(8, 4);                           // 一小块测试图：左暗右亮，中间一条竖直的亮线
    for (int y = 0; y < frame.h; ++y)
        for (int x = 0; x < frame.w; ++x)
            frame.px[static_cast<size_t>(y) * frame.w + x] = static_cast<uint8_t>(x == 4 ? 240 : 20 + x * 10);

    Thumbnailer thumb(12, 6, makeScaler("nearest"));
    for (std::string arg : {"nearest", "bilinear", "bicubic"}) {
        thumb.setScaler(makeScaler(arg));        // 运行时换算法
        Plane big = thumb.make(frame);
        std::printf("%-8s taps=%-2d 第一行:", thumb.scaler().name(), thumb.scaler().taps());
        for (int x = 0; x < big.w; ++x) std::printf(" %3d", big.at(x, 0));
        std::printf("\n");
    }
}
```

输出和 `switch` 版逐个像素相同。变化有这几处：

- 原来分散在 4 个函数里的分支，现在按**算法**收拢：`Bicubic` 类里有它的名字、读几个像素、怎么采样，还有它私有的 `cubic()` 核函数。和一种算法有关的东西都在一个类里。
- 外层循环（把输出坐标映射回源图）每种算法都一样，提出来写成函数模板 `resample()`，每个策略把自己的核函数以 lambda 传进去。
- `Thumbnailer` 只认识 `Scaler` 接口，它不知道有几种算法。`setScaler()` 让它在运行时换算法。
- 名字到策略的对应关系集中在 `makeScaler()` 的一张表里（《简单工厂与工厂方法》里的注册表写法）。命令行、配置文件、界面的下拉框都从这里拿策略。

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

和装饰器、代理不同，具体策略**不持有**另一个 `Scaler`，它们只是被上下文持有、被调用。上下文和策略之间的协作很简单：上下文在需要时调用策略，把需要的数据作为参数传过去。换策略就是换一个指针：

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

### 2.4 实测：加一种 Lanczos

回到 1.2 节的表：策略版的 3 处改动是新增的 `Lanczos` 类、`makeScaler()` 表里的一行、`main()` 列表里的一项。`Scaler`、`Nearest`、`Bilinear`、`Bicubic`、`Thumbnailer` 一行都没动，已有的测试不用重跑。完整代码：

<details>
<summary>展开 strategy_scaler_lanczos.cpp</summary>

```cpp
// 《策略模式》：策略版加一种 Lanczos 算法——新增一个类，再在表里登记一行（和 strategy_scaler.cpp 对比）
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct Plane {                                   // 一个 8 位灰度平面（视频帧的 Y 分量）
    int w, h;
    std::vector<uint8_t> px;
    Plane(int w_, int h_) : w(w_), h(h_), px(static_cast<size_t>(w_) * h_) {}
    int at(int x, int y) const {                 // 越界时取边上的像素
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return px[static_cast<size_t>(y) * w + x];
    }
};

// 策略接口：一种缩放算法
class Scaler {
public:
    virtual ~Scaler() = default;
    virtual const char* name() const = 0;
    virtual int taps() const = 0;                // 每个输出像素读几个源像素
    virtual Plane scale(const Plane& src, int w, int h) const = 0;
};

// 各个算法共用的外层循环：把输出坐标映射回源图，交给 kernel 取值
template <class Kernel>
Plane resample(const Plane& src, int w, int h, Kernel kernel) {
    Plane dst(w, h);
    float rx = static_cast<float>(src.w) / w, ry = static_cast<float>(src.h) / h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float v = kernel(src, (x + 0.5f) * rx - 0.5f, (y + 0.5f) * ry - 0.5f);
            dst.px[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(std::clamp(v + 0.5f, 0.f, 255.f));
        }
    return dst;
}

class Nearest : public Scaler {
public:
    const char* name() const override { return "nearest"; }
    int taps() const override { return 1; }
    Plane scale(const Plane& src, int w, int h) const override {
        return resample(src, w, h, [](const Plane& p, float x, float y) -> float {
            return p.at(static_cast<int>(std::floor(x + 0.5f)), static_cast<int>(std::floor(y + 0.5f)));
        });
    }
};

class Bilinear : public Scaler {
public:
    const char* name() const override { return "bilinear"; }
    int taps() const override { return 4; }
    Plane scale(const Plane& src, int w, int h) const override {
        return resample(src, w, h, [](const Plane& p, float x, float y) {
            int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
            float fx = x - x0, fy = y - y0;
            float top = p.at(x0, y0) * (1 - fx) + p.at(x0 + 1, y0) * fx;
            float bot = p.at(x0, y0 + 1) * (1 - fx) + p.at(x0 + 1, y0 + 1) * fx;
            return top * (1 - fy) + bot * fy;
        });
    }
};

class Bicubic : public Scaler {
public:
    const char* name() const override { return "bicubic"; }
    int taps() const override { return 16; }
    Plane scale(const Plane& src, int w, int h) const override {
        return resample(src, w, h, [](const Plane& p, float x, float y) {
            int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
            float fx = x - x0, fy = y - y0;
            float wx[4], wy[4], sum = 0;                // 横竖各 4 个权重，先算好
            for (int i = 0; i < 4; ++i) wx[i] = cubic(fx - (i - 1)), wy[i] = cubic(fy - (i - 1));
            for (int j = 0; j < 4; ++j)
                for (int i = 0; i < 4; ++i) sum += p.at(x0 + i - 1, y0 + j - 1) * wx[i] * wy[j];
            return sum;
        });
    }

private:
    static float cubic(float t) {                // Keys 三次卷积核，a = -0.5
        t = std::fabs(t);
        if (t < 1) return (1.5f * t - 2.5f) * t * t + 1;
        if (t < 2) return ((-0.5f * t + 2.5f) * t - 4) * t + 2;
        return 0;
    }
};

class Lanczos : public Scaler {
public:
    const char* name() const override { return "lanczos"; }
    int taps() const override { return 36; }
    Plane scale(const Plane& src, int w, int h) const override {
        return resample(src, w, h, [](const Plane& p, float x, float y) {
            int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
            float fx = x - x0, fy = y - y0;
            float wx[6], wy[6], sx = 0, sy = 0, sum = 0;
            for (int i = 0; i < 6; ++i) {
                wx[i] = lanczos3(fx - (i - 2)), wy[i] = lanczos3(fy - (i - 2));
                sx += wx[i], sy += wy[i];
            }
            for (int j = 0; j < 6; ++j)
                for (int i = 0; i < 6; ++i) sum += p.at(x0 + i - 2, y0 + j - 2) * wx[i] * wy[j];
            return sum / (sx * sy);                      // 权重之和不是 1，要归一化
        });
    }

private:
    static float lanczos3(float t) {             // sinc(t) · sinc(t / 3)，|t| < 3
        t = std::fabs(t);
        if (t < 1e-6f) return 1;
        if (t >= 3) return 0;
        const float pi = 3.14159265f;
        return 3 * std::sin(pi * t) * std::sin(pi * t / 3) / (pi * pi * t * t);
    }
};

// 名字 → 策略：命令行、配置文件、界面下拉框都从这里取
std::unique_ptr<Scaler> makeScaler(const std::string& name) {
    static const std::map<std::string, std::function<std::unique_ptr<Scaler>()>> table = {
        {"nearest", [] { return std::make_unique<Nearest>(); }},
        {"bilinear", [] { return std::make_unique<Bilinear>(); }},
        {"bicubic", [] { return std::make_unique<Bicubic>(); }},
        {"lanczos", [] { return std::make_unique<Lanczos>(); }},
    };
    auto it = table.find(name);
    return it != table.end() ? it->second() : std::make_unique<Bilinear>();
}

// 上下文：生成缩略图。它不知道有哪些算法，只知道 Scaler 接口
class Thumbnailer {
public:
    Thumbnailer(int w, int h, std::unique_ptr<Scaler> s) : w_(w), h_(h), scaler_(std::move(s)) {}
    void setScaler(std::unique_ptr<Scaler> s) { scaler_ = std::move(s); }
    const Scaler& scaler() const { return *scaler_; }
    Plane make(const Plane& frame) const { return scaler_->scale(frame, w_, h_); }

private:
    int w_, h_;
    std::unique_ptr<Scaler> scaler_;
};

int main() {
    Plane frame(8, 4);                           // 一小块测试图：左暗右亮，中间一条竖直的亮线
    for (int y = 0; y < frame.h; ++y)
        for (int x = 0; x < frame.w; ++x)
            frame.px[static_cast<size_t>(y) * frame.w + x] = static_cast<uint8_t>(x == 4 ? 240 : 20 + x * 10);

    Thumbnailer thumb(12, 6, makeScaler("nearest"));
    for (std::string arg : {"nearest", "bilinear", "bicubic", "lanczos"}) {
        thumb.setScaler(makeScaler(arg));        // 运行时换算法
        Plane big = thumb.make(frame);
        std::printf("%-8s taps=%-2d 第一行:", thumb.scaler().name(), thumb.scaler().taps());
        for (int x = 0; x < big.w; ++x) std::printf(" %3d", big.at(x, 0));
        std::printf("\n");
    }
}
```

</details>

```
nearest  taps=1  第一行:  20  30  30  40  50  50 240  70  70  80  90  90
bilinear taps=4  第一行:  20  25  32  38  45  82 208 155  72  78  85  90
bicubic  taps=16 第一行:  19  24  32  38  34  75 227 166  61  76  86  91
lanczos  taps=36 第一行:  19  24  32  43  21  82 230 175  53  71  90  91
```

和 `switch` 版加完 Lanczos 的输出（`switch_scaler_lanczos.cpp`）完全相同。如果再进一步，把每个策略放进自己的 `.cpp`、用《简单工厂与工厂方法》里的自注册写法，连 `makeScaler()` 那一行也不用改，加一种算法就是加一个文件。

### 2.5 四种策略的速度和画质

为什么要同时留着四种算法？用真实画面量一量。`quality.cpp` 读入 `bbb-10s.mp4` 第 5 秒的一帧（`ffmpeg` 转成灰度 PGM），做两件事：

- **速度**：用每种策略把这一帧从 1280×720 放大到 1920×1080，7 次取中位数；
- **画质**：先 2×2 平均缩小到 640×360，再用每种策略放大回 1280×720，和原图比 PSNR（越高越接近原图）。

<details>
<summary>展开 quality.cpp</summary>

<!-- run: off -->
```cpp
// 《策略模式》：用 bbb-10s.mp4 的一帧真实画面，比较四种缩放策略的耗时和画质（PSNR），请用 -O2
//   ffmpeg -ss 5 -i bbb-10s.mp4 -frames:v 1 -pix_fmt gray frame.pgm && ./quality frame.pgm [输出目录]
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

struct Plane {
    int w, h;
    std::vector<uint8_t> px;
    Plane(int w_, int h_) : w(w_), h(h_), px(static_cast<size_t>(w_) * h_) {}
    int at(int x, int y) const {
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return px[static_cast<size_t>(y) * w + x];
    }
};

class Scaler {
public:
    virtual ~Scaler() = default;
    virtual const char* name() const = 0;
    virtual Plane scale(const Plane& src, int w, int h) const = 0;
};

template <class Kernel>
Plane resample(const Plane& src, int w, int h, Kernel kernel) {
    Plane dst(w, h);
    float rx = static_cast<float>(src.w) / w, ry = static_cast<float>(src.h) / h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float v = kernel(src, (x + 0.5f) * rx - 0.5f, (y + 0.5f) * ry - 0.5f);
            dst.px[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(std::clamp(v + 0.5f, 0.f, 255.f));
        }
    return dst;
}

float cubic(float t) {
    t = std::fabs(t);
    if (t < 1) return (1.5f * t - 2.5f) * t * t + 1;
    if (t < 2) return ((-0.5f * t + 2.5f) * t - 4) * t + 2;
    return 0;
}
float lanczos3(float t) {
    t = std::fabs(t);
    if (t < 1e-6f) return 1;
    if (t >= 3) return 0;
    const float pi = 3.14159265f;
    return 3 * std::sin(pi * t) * std::sin(pi * t / 3) / (pi * pi * t * t);
}

struct Nearest : Scaler {
    const char* name() const override { return "nearest"; }
    Plane scale(const Plane& s, int w, int h) const override {
        return resample(s, w, h, [](const Plane& p, float x, float y) -> float {
            return p.at(static_cast<int>(std::floor(x + 0.5f)), static_cast<int>(std::floor(y + 0.5f)));
        });
    }
};
struct Bilinear : Scaler {
    const char* name() const override { return "bilinear"; }
    Plane scale(const Plane& s, int w, int h) const override {
        return resample(s, w, h, [](const Plane& p, float x, float y) {
            int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
            float fx = x - x0, fy = y - y0;
            float top = p.at(x0, y0) * (1 - fx) + p.at(x0 + 1, y0) * fx;
            float bot = p.at(x0, y0 + 1) * (1 - fx) + p.at(x0 + 1, y0 + 1) * fx;
            return top * (1 - fy) + bot * fy;
        });
    }
};
struct Bicubic : Scaler {
    const char* name() const override { return "bicubic"; }
    Plane scale(const Plane& s, int w, int h) const override {
        return resample(s, w, h, [](const Plane& p, float x, float y) {
            int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
            float fx = x - x0, fy = y - y0;
            float wx[4], wy[4], sum = 0;                // 横竖各 4 个权重，先算好
            for (int i = 0; i < 4; ++i) wx[i] = cubic(fx - (i - 1)), wy[i] = cubic(fy - (i - 1));
            for (int j = 0; j < 4; ++j)
                for (int i = 0; i < 4; ++i) sum += p.at(x0 + i - 1, y0 + j - 1) * wx[i] * wy[j];
            return sum;
        });
    }
};
struct Lanczos : Scaler {
    const char* name() const override { return "lanczos"; }
    Plane scale(const Plane& s, int w, int h) const override {
        return resample(s, w, h, [](const Plane& p, float x, float y) {
            int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
            float fx = x - x0, fy = y - y0;
            float wx[6], wy[6], sx = 0, sy = 0, sum = 0;
            for (int i = 0; i < 6; ++i) {
                wx[i] = lanczos3(fx - (i - 2)), wy[i] = lanczos3(fy - (i - 2));
                sx += wx[i], sy += wy[i];
            }
            for (int j = 0; j < 6; ++j)
                for (int i = 0; i < 6; ++i) sum += p.at(x0 + i - 2, y0 + j - 2) * wx[i] * wy[j];
            return sum / (sx * sy);                      // 权重之和不是 1，要归一化
        });
    }
};

Plane readPgm(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::string magic; int w = 0, h = 0, maxv = 0;
    if (!(in >> magic >> w >> h >> maxv) || magic != "P5") return Plane(0, 0);
    in.get();
    Plane p(w, h);
    in.read(reinterpret_cast<char*>(p.px.data()), static_cast<std::streamsize>(p.px.size()));
    return p;
}
void writePgm(const std::string& path, const Plane& p) {
    std::ofstream out(path, std::ios::binary);
    out << "P5\n" << p.w << ' ' << p.h << "\n255\n";
    out.write(reinterpret_cast<const char*>(p.px.data()), static_cast<std::streamsize>(p.px.size()));
}

Plane halve(const Plane& s) {                    // 2×2 求平均，缩小到一半
    Plane d(s.w / 2, s.h / 2);
    for (int y = 0; y < d.h; ++y)
        for (int x = 0; x < d.w; ++x)
            d.px[static_cast<size_t>(y) * d.w + x] = static_cast<uint8_t>(
                (s.at(2 * x, 2 * y) + s.at(2 * x + 1, 2 * y) + s.at(2 * x, 2 * y + 1) + s.at(2 * x + 1, 2 * y + 1) + 2) / 4);
    return d;
}
double psnr(const Plane& a, const Plane& b) {
    double se = 0;
    for (size_t i = 0; i < a.px.size(); ++i) { double d = a.px[i] - b.px[i]; se += d * d; }
    return 10 * std::log10(255.0 * 255.0 / (se / static_cast<double>(a.px.size())));
}
Plane crop(const Plane& s, int x0, int y0, int w, int h) {
    Plane d(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) d.px[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(s.at(x0 + x, y0 + y));
    return d;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("用法：quality frame.pgm [输出目录]\n"); return 1; }
    Plane frame = readPgm(argv[1]);
    if (frame.w < 16 || frame.h < 16) { std::printf("读不到 %s\n", argv[1]); return 1; }
    Plane small = halve(frame);
    std::unique_ptr<Scaler> all[] = {std::make_unique<Nearest>(), std::make_unique<Bilinear>(),
                                     std::make_unique<Bicubic>(), std::make_unique<Lanczos>()};
    for (auto& s : all) s->scale(frame, 1920, 1080);              // 热身

    std::printf("%-9s %14s %20s\n", "策略", "720p→1080p ms", "缩小一半再放大 PSNR dB");
    for (auto& s : all) {
        std::vector<double> t;
        for (int i = 0; i < 7; ++i) {
            auto t0 = std::chrono::steady_clock::now();
            Plane out = s->scale(frame, 1920, 1080);
            t.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        }
        std::sort(t.begin(), t.end());
        Plane back = s->scale(small, frame.w, frame.h);
        std::printf("%-9s %14.2f %20.2f\n", s->name(), t[3], psnr(frame, back));
        if (argc > 2) {                          // 把眼睛附近 40×24 的一块放大 6 倍存下来，配图用
            Plane piece = crop(small, 268, 36, 40, 24);
            writePgm(std::string(argv[2]) + "/" + s->name() + ".pgm", s->scale(piece, 240, 144));
        }
    }
}
```

</details>

<figure class="diagram">
<svg viewBox="0 0 720 186" role="img" aria-label="四种缩放策略把一帧 720p 放大到 1080p 的耗时。Clang：nearest 0.97、bilinear 2.65、bicubic 20.40、lanczos 93.34 毫秒；GCC：1.47、2.77、20.84、112.53 毫秒"><g style="font-family:var(--mono)"><text x="178.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang 21 / macOS</text><rect x="178.0" y="30.0" width="1.5" height="20.0" rx="2" style="fill:var(--warn)"/><text x="185.5" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">0.97 ms</text><rect x="178.0" y="60.0" width="3.8" height="20.0" rx="2" style="fill:var(--accent)"/><text x="187.8" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">2.65 ms</text><rect x="178.0" y="90.0" width="29.4" height="20.0" rx="2" style="fill:var(--link)"/><text x="213.4" y="105.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">20.40 ms</text><rect x="178.0" y="120.0" width="134.4" height="20.0" rx="2" style="fill:var(--danger)"/><text x="318.4" y="135.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">93.34 ms</text><text x="448.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC 13 / Ubuntu</text><rect x="448.0" y="30.0" width="2.1" height="20.0" rx="2" style="fill:var(--warn)"/><text x="456.1" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">1.47 ms</text><rect x="448.0" y="60.0" width="4.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="458.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">2.77 ms</text><rect x="448.0" y="90.0" width="30.0" height="20.0" rx="2" style="fill:var(--link)"/><text x="484.0" y="105.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">20.84 ms</text><rect x="448.0" y="120.0" width="162.0" height="20.0" rx="2" style="fill:var(--danger)"/><text x="616.0" y="135.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">112.53 ms</text><text x="170.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">nearest</text><text x="170.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">bilinear</text><text x="170.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">bicubic</text><text x="170.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">lanczos</text><text x="10.0" y="178.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">quality：bbb-10s.mp4 第 5 秒的一帧（灰度），720p → 1080p，-O2，7 次取中位数；朴素实现，没有 SIMD</text></g></svg>
</figure>

| 策略 | Clang：720p→1080p | GCC：720p→1080p | 缩小一半再放大的 PSNR |
| --- | --- | --- | --- |
| nearest | 0.97 ms | 1.47 ms | 41.99 dB |
| bilinear | 2.65 ms | 2.77 ms | 47.96 dB |
| bicubic | 20.40 ms | 20.84 ms | 49.83 dB |
| lanczos | 93.34 ms | 112.53 ms | 50.06 dB |

（Apple M6，`-O2`；GCC 运行在同一台机器上的 Ubuntu 24.04 容器里。PSNR 两边完全相同。）

把缩小后的眼睛附近（40×24 个像素）放大 6 倍，从左到右是 nearest、bilinear、bicubic、lanczos：

<figure>
<img src="/img/dp/strategy-eye.jpg" alt="大兔子眼睛附近的一小块放大 6 倍：最近邻是一个个方块，双线性平滑但模糊，双三次和 lanczos 的眼眶边缘更清楚，两者几乎看不出差别" width="680">
<figcaption>左起：nearest、bilinear、bicubic、lanczos</figcaption>
</figure>

- 从 nearest 到 lanczos，画质高了 8 dB，耗时多了将近 100 倍。bicubic 到 lanczos 再花约 5 倍的时间，只换来 0.2 dB，眼睛基本看不出来。
- 30 fps 的视频每帧只有 33.3 ms：bicubic 加上解码还来得及，lanczos 单是缩放就超了。所以实时播放用 bilinear 或 bicubic，导出截图、生成封面这种不赶时间的场合才用 lanczos。
- 这里是朴素的 C++ 实现，没有 SIMD。libswscale 在创建 `SwsContext` 时就把每一列、每一行的权重算好，再用手写的汇编，放大一帧 720p 只要 0.2～0.5 ms（见《像素格式转换与重采样》）。快慢的绝对值不同，「越清楚越慢、需要按场合选」这个结论是一样的。

### 2.6 运行时切换：掉帧时自动降级

既然不同场合要用不同的策略，「谁来选」就成了问题。前面是 `main` 按命令行参数选；播放器还可以自己选：用户在设置里选了「高画质」（bicubic），碰到复杂的画面、解码变慢、一帧来不及时，临时降到 bilinear，缓过来再升回去。每个策略的耗时取上一节实测的量级：

```cpp
// 《策略模式》：播放器在运行时换策略——画面复杂、一帧来不及时从 bicubic 降到 bilinear，缓过来再升回去
#include <cstdio>
#include <memory>
#include <vector>

// 这里不真的缩放，每个策略返回「这一帧花了多少毫秒」，数值取自 quality.cpp 在 720p → 1080p 上的实测量级
class Scaler {
public:
    virtual ~Scaler() = default;
    virtual const char* name() const = 0;
    virtual double scaleFrame() const = 0;
};
struct Nearest : Scaler { const char* name() const override { return "nearest"; }  double scaleFrame() const override { return 1; } };
struct Bilinear : Scaler { const char* name() const override { return "bilinear"; } double scaleFrame() const override { return 3; } };
struct Bicubic : Scaler { const char* name() const override { return "bicubic"; }  double scaleFrame() const override { return 21; } };
struct Lanczos : Scaler { const char* name() const override { return "lanczos"; }  double scaleFrame() const override { return 95; } };

class Player {
public:
    explicit Player(double budgetMs) : budget_(budgetMs) {
        ladder_.push_back(std::make_unique<Nearest>());      // 从便宜到贵排好
        ladder_.push_back(std::make_unique<Bilinear>());
        ladder_.push_back(std::make_unique<Bicubic>());
        ladder_.push_back(std::make_unique<Lanczos>());
    }
    void prefer(int level) { preferred_ = cur_ = level; }    // 用户在设置里选的画质

    void playFrame(int n, double decodeMs) {
        double total = decodeMs + ladder_[cur_]->scaleFrame();
        std::printf("帧 %2d  解码 %4.1f + %-8s → %5.1f ms", n, decodeMs, ladder_[cur_]->name(), total);
        if (total > budget_) {
            ++late_; calm_ = 0;
            std::printf("  超时");
            if (late_ >= 2 && cur_ > 0) { --cur_; late_ = 0; std::printf("  → 降级为 %s", ladder_[cur_]->name()); }
        } else {
            late_ = 0;
            bool upFits = cur_ < preferred_ && decodeMs + ladder_[cur_ + 1]->scaleFrame() < budget_ * 0.9;
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
    Player player(1000.0 / 30);                             // 30 fps：每帧 33.3 ms
    player.prefer(2);                                       // 用户选了「高画质」：bicubic
    double decode[] = {6, 7, 6, 15, 16, 15, 14, 9, 6, 6, 7, 6, 6, 7};   // 第 4～7 帧是复杂的画面
    for (int i = 0; i < 14; ++i) player.playFrame(i + 1, decode[i]);
}
```

```
帧  1  解码  6.0 + bicubic  →  27.0 ms
帧  2  解码  7.0 + bicubic  →  28.0 ms
帧  3  解码  6.0 + bicubic  →  27.0 ms
帧  4  解码 15.0 + bicubic  →  36.0 ms  超时
帧  5  解码 16.0 + bicubic  →  37.0 ms  超时  → 降级为 bilinear
帧  6  解码 15.0 + bilinear →  18.0 ms
帧  7  解码 14.0 + bilinear →  17.0 ms
帧  8  解码  9.0 + bilinear →  12.0 ms
帧  9  解码  6.0 + bilinear →   9.0 ms
帧 10  解码  6.0 + bilinear →   9.0 ms  → 恢复为 bicubic
帧 11  解码  7.0 + bicubic  →  28.0 ms
帧 12  解码  6.0 + bicubic  →  27.0 ms
帧 13  解码  6.0 + bicubic  →  27.0 ms
帧 14  解码  7.0 + bicubic  →  28.0 ms
```

复杂画面只有两帧超时；如果一直用 bicubic，第 4～7 帧会全部超时。下面的演示把这两件事放在一起：「换算法」看三种策略在源图上各读了哪些像素、给出什么值；「掉帧时降级」看 `Player` 怎么在运行时换策略：

<div class="dsviz" data-viz="dp-strategy"></div>

注意**选择策略的逻辑在上下文里**（`Player::playFrame`），四个策略类互相不知道对方，也不知道自己什么时候会被换掉。这一点是策略和第 5 节「状态模式」的关键区别。

## 3. 深入策略模式

### 3.1 标准库里的策略：编译期选好

标准库里到处是策略，只是大多是**模板参数**，编译时就选定了：

| 标准库 | 策略是什么 | 例子 |
| --- | --- | --- |
| `std::sort(first, last, comp)` | 怎么比较两个元素 | `std::greater<>{}`、按时间戳比较帧的 lambda |
| `std::unique_ptr<T, Deleter>` | 怎么释放 | 包装 `av_frame_free` 的删除器 |
| `std::unordered_map<K, V, Hash, KeyEqual>` | 怎么算哈希、怎么判断相等 | 按文件路径忽略大小写 |
| `std::vector<T, Allocator>` | 怎么分配内存 | 内存池、对齐分配器 |
| `std::basic_string<C, Traits>` | 字符怎么比较 | 忽略大小写的 `char_traits` |

这种「把一个类的某个行为抽成模板参数」的写法，在 C++ 里常叫**策略类**（policy class）或基于策略的设计（policy-based design）。它和 GoF 的策略是同一个思路，差别在于选择发生在编译期：`std::unique_ptr<Frame, FrameDeleter>` 一旦写定，就不能在运行时换一个删除器；作为交换，它没有任何间接调用，第 4.3 节还会看到它连空间都不占。

### 3.2 FFmpeg 里的策略：函数指针表

C 没有虚函数，FFmpeg 用**一组函数指针**表达同样的意思。每个解码器在 FFmpeg 内部是一个 `FFCodec` 结构体，里面放着 `init`、`decode`（或 `receive_frame`）、`close` 等函数指针；`avcodec_find_decoder()` 按编码格式找到其中一个，`AVCodecContext` 持有它，解码时通过函数指针调用。H.264、HEVC、AV1 的解码器互相替换，调用方的 `avcodec_send_packet()` / `avcodec_receive_frame()` 不用改。这是用 C 写的策略：结构体就是接口，每个解码器填一份自己的函数。

libswscale 的缩放算法却**不是**这样做的：`sws_getContext(..., SWS_BICUBIC, ...)` 用一个标志位选算法，库的内部按标志位分支，生成对应的滤波系数。用户只能从固定的几种里选，不能把自己写的算法塞进去。这并不是设计失误：缩放算法的集合由 FFmpeg 自己掌控、很少变化，而且选好算法之后，真正热的循环只认滤波系数，不再关心是哪种算法。**集合封闭、由自己掌控时，`enum` 加分支是合理的选择**；需要让别人扩展（插件、用户自定义）时，才需要策略这样的开放结构。

### 3.3 策略对象要不要有状态

到目前为止的策略都**没有状态**：`Bilinear` 没有数据成员，两个 `Thumbnailer` 共用一个 `Bilinear` 对象完全没问题，甚至可以做成全局的单个对象（这时它也是一个享元）。

但有的策略天生有状态：

- 码率控制里的 CBR 要记住之前花了多少比特（练习 3）；
- 为了快，缩放策略可能缓存每一列的权重表（练习 4，libswscale 的 `SwsContext` 也是这么做的，所以它是按「源尺寸 × 目标尺寸」创建的）。

有状态的策略**不能在上下文之间共用**，每个上下文要有自己的一份（`unique_ptr` 独占，或者共用一个工厂，每次创建新对象）；多线程下共用还会出现数据竞争。设计接口时可以用 `const` 表达这一点：`scale()` 声明成 `const` 成员函数，说明它不应该修改策略自己；需要缓存时再加 `mutable`，那就要清楚它不能被共用。

### 3.4 策略要的数据从哪里来

策略干活需要数据，有两种给法：

- **作为参数传进去**：`scale(src, w, h)`。策略和上下文互不依赖，最常用。缺点是不同的策略要的数据不一样时，接口只能取并集：最近邻用不到的参数，也得出现在签名里。
- **策略持有上下文的引用**，需要什么自己去取。接口简单，但策略和上下文绑在了一起，策略不能脱离上下文测试。

本文的 `taps()` 就是一个接口设计不周的例子：最近邻、双线性、双三次、Lanczos 读的像素数是固定的，所以设计成了没有参数的 `taps()`；练习 2 加一种「区域平均」策略时会发现，它读多少个像素取决于缩小的倍数，接口不得不改。开闭原则只对**预料到的变化**关闭，策略接口的设计要看清楚所有可能的策略需要什么。

### 3.5 什么时候不用策略

- 只有两三种做法，而且确定不会再加：一个 `if` 更直接。
- 算法的集合封闭、由你自己掌控：用 `enum` + `switch` 或 `std::variant`（第 4 节），编译器能检查有没有漏掉的分支。
- 调用方要知道每种策略的细节才能选（比如每种策略要的参数完全不同）：说明它们其实不能「互换」，硬套一个接口只会让接口越来越宽。

## 4. 现代 C++ 写法

《组合优于继承与 C++ 的四种多态》比较过虚函数、`std::function`、模板、`std::variant` 四种多态的一般性质。这一节只看它们**写成策略**时的样子和代价。

### 4.1 同一个上下文的四种写法

```cpp
// 《策略模式》：同一个缩略图生成器的四种写法——虚函数接口、std::function、模板参数、std::variant
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <variant>
#include <vector>

struct Plane {
    int w, h;
    std::vector<uint8_t> px;
    Plane(int w_, int h_) : w(w_), h(h_), px(static_cast<size_t>(w_) * h_) {}
    int at(int x, int y) const {
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return px[static_cast<size_t>(y) * w + x];
    }
};

template <class Kernel>
Plane resample(const Plane& src, int w, int h, Kernel kernel) {
    Plane dst(w, h);
    float rx = static_cast<float>(src.w) / w, ry = static_cast<float>(src.h) / h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float v = kernel(src, (x + 0.5f) * rx - 0.5f, (y + 0.5f) * ry - 0.5f);
            dst.px[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(std::clamp(v + 0.5f, 0.f, 255.f));
        }
    return dst;
}

// 两个算法写成普通的函数对象，四种写法共用
struct NearestK {
    float operator()(const Plane& p, float x, float y) const {
        return p.at(static_cast<int>(std::floor(x + 0.5f)), static_cast<int>(std::floor(y + 0.5f)));
    }
};
struct BilinearK {
    float operator()(const Plane& p, float x, float y) const {
        int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
        float fx = x - x0, fy = y - y0;
        float top = p.at(x0, y0) * (1 - fx) + p.at(x0 + 1, y0) * fx;
        float bot = p.at(x0, y0 + 1) * (1 - fx) + p.at(x0 + 1, y0 + 1) * fx;
        return top * (1 - fy) + bot * fy;
    }
};

// ① 虚函数接口：经典的 GoF 写法
struct Scaler {
    virtual ~Scaler() = default;
    virtual Plane scale(const Plane& src, int w, int h) const = 0;
};
template <class K> struct KernelScaler : Scaler {
    Plane scale(const Plane& src, int w, int h) const override { return resample(src, w, h, K{}); }
};
class ThumbV {
public:
    explicit ThumbV(std::unique_ptr<Scaler> s) : s_(std::move(s)) {}
    void set(std::unique_ptr<Scaler> s) { s_ = std::move(s); }
    Plane make(const Plane& f) const { return s_->scale(f, 4, 2); }
private:
    std::unique_ptr<Scaler> s_;
};

// ② std::function：策略就是「一个函数」，任何签名对得上的可调用对象都行
using ScaleFn = std::function<Plane(const Plane&, int, int)>;
class ThumbF {
public:
    explicit ThumbF(ScaleFn f) : f_(std::move(f)) {}
    void set(ScaleFn f) { f_ = std::move(f); }
    Plane make(const Plane& f) const { return f_(f, 4, 2); }
private:
    ScaleFn f_;
};

// ③ 模板参数：编译期选定，像 std::sort 的比较函数、unique_ptr 的删除器
template <class Kernel = BilinearK>
class ThumbT {
public:
    Plane make(const Plane& f) const { return resample(f, 4, 2, Kernel{}); }
};

// ④ std::variant：算法的集合是封闭的，按值存放，换算法就是赋一个新值
using AnyKernel = std::variant<NearestK, BilinearK>;
class ThumbVar {
public:
    explicit ThumbVar(AnyKernel k) : k_(k) {}
    void set(AnyKernel k) { k_ = k; }
    Plane make(const Plane& f) const {
        return std::visit([&](auto k) { return resample(f, 4, 2, k); }, k_);
    }
private:
    AnyKernel k_;
};

void show(const char* tag, const Plane& p) {
    std::printf("%-22s", tag);
    for (int x = 0; x < p.w; ++x) std::printf(" %3d", p.at(x, 0));
    std::printf("\n");
}

int main() {
    Plane frame(8, 4);
    for (int y = 0; y < frame.h; ++y)
        for (int x = 0; x < frame.w; ++x)
            frame.px[static_cast<size_t>(y) * frame.w + x] = static_cast<uint8_t>(x == 4 ? 240 : 20 + x * 10);

    ThumbV v(std::make_unique<KernelScaler<NearestK>>());
    show("① 虚函数 nearest", v.make(frame));
    v.set(std::make_unique<KernelScaler<BilinearK>>());
    show("① 虚函数 bilinear", v.make(frame));

    ThumbF f([](const Plane& s, int w, int h) { return resample(s, w, h, NearestK{}); });
    show("② function nearest", f.make(frame));
    float gain = 1.2f;                           // lambda 可以带上自己的参数：先缩放再提亮 20%
    f.set([gain](const Plane& s, int w, int h) {
        return resample(s, w, h, [gain](const Plane& p, float x, float y) { return BilinearK{}(p, x, y) * gain; });
    });
    show("② function 提亮", f.make(frame));

    show("③ 模板 nearest", ThumbT<NearestK>{}.make(frame));
    show("③ 模板 bilinear", ThumbT<>{}.make(frame));

    ThumbVar var(NearestK{});
    show("④ variant nearest", var.make(frame));
    var.set(BilinearK{});
    show("④ variant bilinear", var.make(frame));
}
```

```
① 虚函数 nearest   30  50  70  90
① 虚函数 bilinear  25  45 155  85
② function nearest    30  50  70  90
② function 提亮     30  54 186 102
③ 模板 nearest      30  50  70  90
③ 模板 bilinear     25  45 155  85
④ variant nearest     30  50  70  90
④ variant bilinear    25  45 155  85
```

（缩小到 4×2 时，最近邻正好跳过了那条亮线，第三个值是 70 而不是 240；这就是缩小时的混叠。）

- **① 虚函数**：经典写法。策略可以有好几个函数（`name()`、`taps()`、`scale()`），可以在别的动态库里实现（插件），运行时换。
- **② `std::function`**：策略只有一个函数时最省事，不用定义接口和类，lambda 直接传进去，还能带上自己的参数（`gain`）。《SOLID 原则（上）：单一职责与开闭》里的密码规则列表就是这种写法。
- **③ 模板参数**：编译期选定，不能在运行时换，但编译器能把策略内联进来。`ThumbT<>` 还能有默认策略，像 `std::unique_ptr<T>` 默认用 `std::default_delete<T>`。
- **④ `std::variant`**：策略的集合封闭，按值存放，不用堆分配；换策略就是赋一个新值。加新策略要改 `variant` 的定义，所有 `visit` 的地方编译器都会检查。

### 4.2 调用开销：粒度比写法重要

调用一次策略的开销什么时候才重要？看**调用的次数**。`bench_granularity.cpp` 把 720p 放大到 1080p，同一个双线性（和最近邻）核函数，用不同的方式交给外层循环：

- 0～3：核函数作为策略，每个输出像素调用一次（207 万次），分别用 `switch`、虚函数、`std::function`、`variant` 分派；
- 4：模板参数，编译期就知道是哪个核；
- 5：还是模板参数，但给核函数加了 `noinline`，不让编译器内联；
- 6：策略的粒度放大到「缩放一整帧」，虚函数每帧只调用一次，策略内部的循环用模板。

<figure class="diagram">
<svg viewBox="0 0 750 184" role="img" aria-label="策略的粒度：细粒度时 resample 循环对 1080p 的每个像素调用一次 Kernel::sample，共 2,073,600 次间接调用，核函数无法内联；粗粒度时 Player 每帧只调用一次 Bilinear::scale，循环在策略内部用模板实现，全部内联"><g style="font-family:var(--mono)"><text x="20.0" y="20.0" text-anchor="start" style="fill:var(--fg);font-size:12.5px;font-weight:700">细粒度：策略是「取一个像素」</text><rect x="20" y="36" width="150" height="40" rx="4" style="fill:none;stroke:var(--border);stroke-width:1.3"/><text x="95.0" y="60.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">resample 循环</text><line x1="170" y1="56" x2="373" y2="56" style="stroke:var(--warn);stroke-width:1.6"/><path d="M380,56 L371,51 L371,61 z" style="fill:var(--warn)"/><text x="275.0" y="49.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">sample(x, y) × 2,073,600 次</text><rect x="380" y="36" width="160" height="40" rx="4" style="fill:none;stroke:var(--border);stroke-width:1.3"/><text x="460.0" y="60.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">Kernel::sample()</text><text x="560.0" y="52.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">每次都是一次间接调用，</text><text x="560.0" y="68.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">核函数没法内联进循环</text><text x="20.0" y="112.0" text-anchor="start" style="fill:var(--fg);font-size:12.5px;font-weight:700">粗粒度：策略是「缩放一整帧」</text><rect x="20" y="128" width="150" height="40" rx="4" style="fill:none;stroke:var(--border);stroke-width:1.3"/><text x="95.0" y="152.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">Player</text><line x1="170" y1="148" x2="373" y2="148" style="stroke:var(--accent);stroke-width:1.6"/><path d="M380,148 L371,143 L371,153 z" style="fill:var(--accent)"/><text x="275.0" y="141.0" text-anchor="middle" style="fill:var(--accent);font-size:11.5px;font-weight:700">scale(frame) × 1 次</text><rect x="380" y="128" width="160" height="40" rx="4" style="fill:none;stroke:var(--border);stroke-width:1.3"/><text x="460.0" y="152.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">Bilinear::scale()</text><text x="560.0" y="144.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">虚调用只有一次，</text><text x="560.0" y="160.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">里面的循环用模板，全部内联</text></g></svg>
</figure>

<details>
<summary>展开 bench_granularity.cpp</summary>

<!-- run: off -->
```cpp
// 《策略模式》：同一个双线性/最近邻核，用 switch、虚函数、std::function、variant、模板参数五种方式按像素调用，再和「整帧调用一次虚函数」比（请用 -O2）
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <variant>
#include <vector>

#if defined(__GNUC__)
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif

struct Plane {
    int w, h;
    std::vector<uint8_t> px;
    Plane(int w_, int h_) : w(w_), h(h_), px(static_cast<size_t>(w_) * h_) {}
    int at(int x, int y) const {
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return px[static_cast<size_t>(y) * w + x];
    }
};

inline float nearest(const Plane& p, float x, float y) {
    return p.at(static_cast<int>(std::floor(x + 0.5f)), static_cast<int>(std::floor(y + 0.5f)));
}
inline float bilinear(const Plane& p, float x, float y) {
    int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    float fx = x - x0, fy = y - y0;
    float top = p.at(x0, y0) * (1 - fx) + p.at(x0 + 1, y0) * fx;
    float bot = p.at(x0, y0 + 1) * (1 - fx) + p.at(x0 + 1, y0 + 1) * fx;
    return top * (1 - fy) + bot * fy;
}

// 外层循环：kernel 是怎么传进来的，就是下面几种写法的区别
template <class Sample>
void loop(const Plane& src, Plane& dst, Sample sample) {
    float rx = static_cast<float>(src.w) / dst.w, ry = static_cast<float>(src.h) / dst.h;
    for (int y = 0; y < dst.h; ++y)
        for (int x = 0; x < dst.w; ++x) {
            float v = sample(src, (x + 0.5f) * rx - 0.5f, (y + 0.5f) * ry - 0.5f);
            dst.px[static_cast<size_t>(y) * dst.w + x] = static_cast<uint8_t>(std::clamp(v + 0.5f, 0.f, 255.f));
        }
}

// 0. enum + switch（问题版）
enum class Algo { Nearest, Bilinear };
NOINLINE void scaleSwitch(const Plane& s, Plane& d, Algo a) {
    loop(s, d, [a](const Plane& p, float x, float y) {
        switch (a) {
        case Algo::Nearest: return nearest(p, x, y);
        case Algo::Bilinear: return bilinear(p, x, y);
        }
        return 0.f;
    });
}

// 1. 虚函数，每个像素调用一次
struct Kernel {
    virtual ~Kernel() = default;
    virtual float sample(const Plane& p, float x, float y) const = 0;
};
struct NearestK : Kernel { float sample(const Plane& p, float x, float y) const override { return nearest(p, x, y); } };
struct BilinearK : Kernel { float sample(const Plane& p, float x, float y) const override { return bilinear(p, x, y); } };
NOINLINE void scaleVirtual(const Plane& s, Plane& d, const Kernel& k) {
    loop(s, d, [&k](const Plane& p, float x, float y) { return k.sample(p, x, y); });
}

// 2. std::function，每个像素调用一次
using KernelFn = std::function<float(const Plane&, float, float)>;
NOINLINE void scaleFunction(const Plane& s, Plane& d, const KernelFn& f) {
    loop(s, d, [&f](const Plane& p, float x, float y) { return f(p, x, y); });
}

// 3. std::variant + std::visit，每个像素 visit 一次
struct NearestV { float operator()(const Plane& p, float x, float y) const { return nearest(p, x, y); } };
struct BilinearV { float operator()(const Plane& p, float x, float y) const { return bilinear(p, x, y); } };
using KernelVar = std::variant<NearestV, BilinearV>;
NOINLINE void scaleVariant(const Plane& s, Plane& d, const KernelVar& v) {
    loop(s, d, [&v](const Plane& p, float x, float y) {
        return std::visit([&](const auto& k) { return k(p, x, y); }, v);
    });
}

// 4. 模板参数：编译期就知道是哪个核，可以内联进循环
template <class K>
NOINLINE void scaleTemplate(const Plane& s, Plane& d, K k) { loop(s, d, k); }

// 5. 模板参数，但禁止内联核函数：看看快是因为「没有虚调用」还是因为「内联」
struct NearestNI { NOINLINE float operator()(const Plane& p, float x, float y) const { return nearest(p, x, y); } };
struct BilinearNI { NOINLINE float operator()(const Plane& p, float x, float y) const { return bilinear(p, x, y); } };

// 6. 策略的粒度放大到「整帧」：虚函数每帧只调用一次，里面用模板
struct FrameScaler {
    virtual ~FrameScaler() = default;
    virtual void scale(const Plane& s, Plane& d) const = 0;
};
struct NearestF : FrameScaler { void scale(const Plane& s, Plane& d) const override { loop(s, d, NearestV{}); } };
struct BilinearF : FrameScaler { void scale(const Plane& s, Plane& d) const override { loop(s, d, BilinearV{}); } };

volatile int g_pick = 1;                         // 运行时才决定用哪个核，防止编译器提前看穿

template <class F>
double medianMs(F run) {
    std::vector<double> t;
    run();                                       // 先热身一次
    for (int i = 0; i < 7; ++i) {
        auto t0 = std::chrono::steady_clock::now();
        run();
        t.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
    std::sort(t.begin(), t.end());
    return t[3];
}

int main() {
    Plane src(1280, 720), dst(1920, 1080);       // 720p 的帧放大到 1080p 全屏
    for (int y = 0; y < src.h; ++y)
        for (int x = 0; x < src.w; ++x)
            src.px[static_cast<size_t>(y) * src.w + x] = static_cast<uint8_t>((x * 7 + y * 13 + (x * y) % 31) & 255);

    NearestK nk; BilinearK bk;
    const Kernel* ks[] = {&nk, &bk};
    NearestF nf; BilinearF bf;
    const FrameScaler* fs[] = {&nf, &bf};
    unsigned check = 0;
    auto sum = [&] { for (int i = 0; i < 1920 * 1080; i += 4099) check += dst.px[static_cast<size_t>(i)]; };

    for (int i = 0; i < 50; ++i) scaleTemplate(src, dst, BilinearV{});   // 让 CPU 先升到稳定频率
    std::printf("%-26s %10s %10s   （ms/帧，1280x720 → 1920x1080，7 次中位数）\n", "写法", "nearest", "bilinear");
    auto row = [&](const char* name, auto runFor) {
        double r[2];
        for (int k = 0; k < 2; ++k) { g_pick = k; r[k] = medianMs([&] { runFor(g_pick); }); sum(); }
        std::printf("%-26s %10.2f %10.2f\n", name, r[0], r[1]);
    };
    row("0 enum + switch（每像素）", [&](int k) { scaleSwitch(src, dst, k ? Algo::Bilinear : Algo::Nearest); });
    row("1 虚函数（每像素）", [&](int k) { scaleVirtual(src, dst, *ks[k]); });
    row("2 std::function（每像素）", [&](int k) {
        KernelFn f = k ? KernelFn(BilinearV{}) : KernelFn(NearestV{});
        scaleFunction(src, dst, f);
    });
    row("3 variant + visit（每像素）", [&](int k) {
        KernelVar v = k ? KernelVar(BilinearV{}) : KernelVar(NearestV{});
        scaleVariant(src, dst, v);
    });
    row("4 模板参数", [&](int k) { k ? scaleTemplate(src, dst, BilinearV{}) : scaleTemplate(src, dst, NearestV{}); });
    row("5 模板参数 + 禁止内联", [&](int k) { k ? scaleTemplate(src, dst, BilinearNI{}) : scaleTemplate(src, dst, NearestNI{}); });
    row("6 虚函数（每帧一次）", [&](int k) { fs[k]->scale(src, dst); });
    std::printf("(校验和 %u)\n", check);
}
```

</details>

<figure class="diagram">
<svg viewBox="0 0 760 276" role="img" aria-label="双线性缩放 1280×720 到 1920×1080，每帧毫秒数。Clang：switch 3.03、虚函数每像素 3.55、std::function 3.90、variant 4.08、模板 3.00、模板禁止内联 3.56、虚函数每帧一次 2.96；GCC：3.63、3.61、3.92、3.63、2.82、3.64、2.77"><g style="font-family:var(--mono)"><text x="218.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang 21 / macOS</text><rect x="218.0" y="30.0" width="121.2" height="20.0" rx="2" style="fill:var(--warn)"/><text x="345.2" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">3.03 ms</text><rect x="218.0" y="60.0" width="142.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="366.0" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">3.55 ms</text><rect x="218.0" y="90.0" width="156.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="380.0" y="105.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">3.90 ms</text><rect x="218.0" y="120.0" width="163.2" height="20.0" rx="2" style="fill:var(--warn)"/><text x="387.2" y="135.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">4.08 ms</text><rect x="218.0" y="150.0" width="120.0" height="20.0" rx="2" style="fill:var(--link)"/><text x="344.0" y="165.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">3.00 ms</text><rect x="218.0" y="180.0" width="142.4" height="20.0" rx="2" style="fill:var(--danger)"/><text x="366.4" y="195.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">3.56 ms</text><rect x="218.0" y="210.0" width="118.4" height="20.0" rx="2" style="fill:var(--accent)"/><text x="342.4" y="225.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">2.96 ms</text><text x="488.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC 13 / Ubuntu</text><rect x="488.0" y="30.0" width="145.2" height="20.0" rx="2" style="fill:var(--warn)"/><text x="639.2" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">3.63 ms</text><rect x="488.0" y="60.0" width="144.4" height="20.0" rx="2" style="fill:var(--warn)"/><text x="638.4" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">3.61 ms</text><rect x="488.0" y="90.0" width="156.8" height="20.0" rx="2" style="fill:var(--warn)"/><text x="650.8" y="105.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">3.92 ms</text><rect x="488.0" y="120.0" width="145.2" height="20.0" rx="2" style="fill:var(--warn)"/><text x="639.2" y="135.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">3.63 ms</text><rect x="488.0" y="150.0" width="112.8" height="20.0" rx="2" style="fill:var(--link)"/><text x="606.8" y="165.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">2.82 ms</text><rect x="488.0" y="180.0" width="145.6" height="20.0" rx="2" style="fill:var(--danger)"/><text x="639.6" y="195.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">3.64 ms</text><rect x="488.0" y="210.0" width="110.8" height="20.0" rx="2" style="fill:var(--accent)"/><text x="604.8" y="225.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">2.77 ms</text><text x="210.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">0 enum + switch</text><text x="210.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">1 虚函数（每像素）</text><text x="210.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">2 std::function（每像素）</text><text x="210.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">3 variant + visit（每像素）</text><text x="210.0" y="165.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">4 模板参数</text><text x="210.0" y="195.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">5 模板 + 禁止内联</text><text x="210.0" y="225.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">6 虚函数（每帧一次）</text><text x="10.0" y="268.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench_granularity：双线性，720p 放大到 1080p（207 万个输出像素），-O2，7 次取中位数，越短越快</text></g></svg>
</figure>

| ms/帧（720p → 1080p） | Clang：nearest | Clang：bilinear | GCC：nearest | GCC：bilinear |
| --- | --- | --- | --- | --- |
| 0 `enum` + `switch`（每像素） | 1.92 | 3.03 | 2.21 | 3.63 |
| 1 虚函数（每像素） | 2.19 | 3.55 | 2.38 | 3.61 |
| 2 `std::function`（每像素） | 2.41 | 3.90 | 2.86 | 3.92 |
| 3 `variant` + `visit`（每像素） | 2.71 | 4.08 | 2.27 | 3.63 |
| 4 模板参数 | 1.64 | 3.00 | 1.73 | 2.82 |
| 5 模板参数 + 禁止内联 | 2.06 | 3.56 | 2.37 | 3.64 |
| 6 虚函数（每帧一次） | 1.66 | 2.96 | 1.77 | 2.77 |

（Apple M6，`-O2`，7 次取中位数，重复运行相差在 0.1 ms 以内。）

几个结论：

1. **每个像素调用一次策略，就要付每个像素的代价**。双线性下，虚函数、`std::function`、`variant` 比模板参数慢 18%～36%（Clang）、28%～39%（GCC）。按 Clang 的虚函数算，每次调用多花约 0.27 ns，207 万次就是半毫秒。
2. **贵的不是「虚」，是「不能内联」**。第 5 行的模板版禁止内联以后，和第 1 行的虚函数几乎一样慢（Clang 3.56 对 3.55，GCC 3.64 对 3.61）。核函数内联进循环以后，编译器才能把坐标计算、边界检查一起优化。
3. **把策略的粒度放大到整帧，虚函数就不贵了**。第 6 行每帧只有一次虚调用，和纯模板版一样快（Clang 2.96 对 3.00，GCC 2.77 对 2.82），同时保留了运行时换策略的能力。这就是本文 `Scaler` 的设计：接口是 `scale()` 整帧，循环在策略内部用 `resample()` 模板。
4. **同样的写法，两个编译器不一样**。GCC 下 `variant` 和 `switch` 一样快（3.63），Clang 下 `variant` 是最慢的（4.08）；最近邻的 `switch`，Clang 比 GCC 快。具体快多少要在自己的编译器上量，「粒度决定开销」这个结论两边一致。

所以设计策略接口时，先问一句：**它会被调用多少次？**一帧一次、一个文件一次的策略，用最灵活的虚函数或 `std::function`；每个像素、每个采样点都要调用的，要么用模板，要么像 `Scaler` 这样把粒度放大，让热循环留在策略内部。

### 4.3 对象有多大、要不要分配内存

`sizes.cpp` 比较四种写法下，一个「两个 `int` 加一个策略」的上下文有多大，以及 `std::function` 什么时候要在堆上分配：

<details>
<summary>展开 sizes.cpp</summary>

<!-- run: off -->
```cpp
// 《策略模式》：四种写法下，持有一个策略的上下文对象有多大；std::function 什么时候要分配堆内存
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <new>
#include <variant>

static int g_news = 0;                           // 数一数 operator new 被调用了几次
void* operator new(std::size_t n) {
    ++g_news;
    if (void* p = std::malloc(n)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

struct Plane;                                    // 这里只看大小，不需要真的缩放
struct Scaler { virtual ~Scaler() = default; virtual void scale(const Plane&) const = 0; };
struct Bilinear : Scaler { void scale(const Plane&) const override {} };
struct BilinearFn { void operator()(const Plane&) const {} };      // 空的函数对象：没有数据成员
struct LanczosFn { int a = 3; void operator()(const Plane&) const {} };

// 四种上下文，都另有两个 int（目标宽高）
struct CtxVirtual  { int w, h; std::unique_ptr<Scaler> s; };
struct CtxFunction { int w, h; std::function<void(const Plane&)> s; };
template <class S> struct CtxTemplate { int w, h; S s; };
template <class S> struct CtxTemplateEbo : private S { int w, h; };  // 空基类优化：空策略不占空间
struct CtxVariant  { int w, h; std::variant<BilinearFn, LanczosFn> s; };

// unique_ptr 的删除器也是一个编译期策略
struct Frame { int dummy; };
void frameFree(Frame* f) { delete f; }
struct FrameDeleter { void operator()(Frame* f) const { delete f; } };

int main() {
    std::printf("sizeof(CtxVirtual)             = %2zu  (+ 堆上的策略对象 %zu)\n", sizeof(CtxVirtual), sizeof(Bilinear));
    std::printf("sizeof(CtxFunction)            = %2zu\n", sizeof(CtxFunction));
    std::printf("sizeof(CtxTemplate<Bilinear>)  = %2zu\n", sizeof(CtxTemplate<BilinearFn>));
    std::printf("sizeof(CtxTemplateEbo<...>)    = %2zu\n", sizeof(CtxTemplateEbo<BilinearFn>));
    std::printf("sizeof(CtxVariant)             = %2zu\n", sizeof(CtxVariant));

    std::printf("\nunique_ptr<Frame>                       = %2zu\n", sizeof(std::unique_ptr<Frame>));
    std::printf("unique_ptr<Frame, FrameDeleter>         = %2zu\n", sizeof(std::unique_ptr<Frame, FrameDeleter>));
    std::printf("unique_ptr<Frame, void(*)(Frame*)>      = %2zu\n", sizeof(std::unique_ptr<Frame, void (*)(Frame*)>));
    std::printf("unique_ptr<Frame, function<...>>        = %2zu\n", sizeof(std::unique_ptr<Frame, std::function<void(Frame*)>>));
    std::unique_ptr<Frame, void (*)(Frame*)> f(new Frame{}, frameFree);

    std::printf("\nstd::function 捕获不同大小的数据时 new 了几次：\n");
    struct B8 { char d[8]; }; struct B16 { char d[16]; }; struct B24 { char d[24]; }; struct B32 { char d[32]; };
    auto test = [](const char* name, auto blob) {
        int before = g_news;
        std::function<int(const Plane&)> fn = [blob](const Plane&) { return blob.d[0] + static_cast<int>(sizeof blob); };
        std::printf("  捕获 %-8s → %d 次\n", name, g_news - before);
    };
    test("8 字节", B8{});
    test("16 字节", B16{});
    test("24 字节", B24{});
    test("32 字节", B32{});
}
```

</details>

```
sizeof(CtxVirtual)             = 16  (+ 堆上的策略对象 8)
sizeof(CtxFunction)            = 40
sizeof(CtxTemplate<Bilinear>)  = 12
sizeof(CtxTemplateEbo<...>)    =  8
sizeof(CtxVariant)             = 16

unique_ptr<Frame>                       =  8
unique_ptr<Frame, FrameDeleter>         =  8
unique_ptr<Frame, void(*)(Frame*)>      = 16
unique_ptr<Frame, function<...>>        = 40

std::function 捕获不同大小的数据时 new 了几次：
  捕获 8 字节 → 0 次
  捕获 16 字节 → 0 次
  捕获 24 字节 → 1 次
  捕获 32 字节 → 1 次
```

Clang（libc++）和 GCC（libstdc++）的输出完全相同：

- **虚函数**：上下文里是一个 8 字节的指针，策略对象另外在堆上（至少有一个 8 字节的虚表指针）。
- **`std::function`** 本身占 32 字节。捕获的数据不超过 16 字节时，直接放在这 32 字节里面（小对象优化）；超过了就要 `new` 一次。lambda 捕获了一张权重表，就会在每次构造、拷贝 `std::function` 时分配内存。
- **模板参数**：空的策略类按规则也要占 1 个字节，对齐以后上下文从 8 变成 12；继承这个空类（空基类优化），它就不占空间了。`unique_ptr<Frame, FrameDeleter>` 和裸指针一样大，就是靠这个（标准库内部把删除器和指针压在一起存）。换成函数指针 `void(*)(Frame*)`，删除器就要占 8 字节，因为指针的值只有运行时才知道。
- **`variant`**：大小是最大的备选类型加一个标记，这里 4 字节的 `LanczosFn` 加标记，对齐到 8。

### 4.4 怎么选

| 写法 | 运行时换 | 策略有多个函数 | 别人能扩展 | 开销 | 适合 |
| --- | --- | --- | --- | --- | --- |
| 虚函数接口 | 可以 | 可以 | 可以（插件） | 每次调用一次间接跳转 | 粗粒度的策略、需要插件 |
| `std::function` | 可以 | 不行 | 可以 | 同上，捕获多了要分配 | 只有一个函数的策略、回调 |
| 模板参数 | 不行 | 可以 | 可以（编译期） | 没有，能内联 | 细粒度、热循环、标准库式的 policy |
| `std::variant` | 可以 | 可以（`visit`） | 不行（集合封闭） | 一次分支 | 策略固定的几种、要按值存放 |

## 5. 策略和相似的模式

| 模式 | 结构 | 谁决定换 | 关注点 |
| --- | --- | --- | --- |
| **策略** | 上下文持有一个算法接口 | 上下文或客户；策略之间互不知道 | 同一件事的几种**做法**，可以互换 |
| 状态 | 上下文持有一个状态接口 | 状态对象自己决定下一个状态 | 对象在不同**状态**下行为不同 |
| 模板方法 | 基类写好流程，子类重写其中几步 | 编译期，由用哪个子类决定 | 流程固定，个别**步骤**可变 |
| 桥接 | 抽象持有实现接口，两边各有子类 | 通常创建时决定 | 两个独立变化的**维度** |

- **策略和状态**：类图几乎一样，区别在「谁来换、换了以后知不知道」。策略的几个实现是平等的选项，互不知道对方，由外面来选；状态的几个实现知道彼此，`Playing::pause()` 会把上下文切换成 `Paused`。第 2.6 节的降级逻辑写在 `Player` 里，所以是策略；如果写成「`Bicubic` 发现自己超时了，把 `Player` 换成 `Bilinear`」，就成了状态。
- **策略和模板方法**：都是把「可变的部分」分出去。模板方法用继承：基类的 `scale()` 写好外层循环，子类重写 `sample()`；策略用组合：上下文持有一个策略对象。本文的 `resample()` 函数模板其实是两者的折中：外层循环固定，核函数作为参数传进来。模板方法是第 4 章的第 4 篇。
- **策略和桥接**：《桥接模式》第 5 节说过，两者的类图几乎一样。策略替换的是一个算法，上下文通常只有一个类；桥接的两边都是继承体系，实现那一边是一整套基本操作。
- **策略和命令**：都是把「一段行为」做成对象。策略回答「这件事**怎么**做」，几种做法可以互换；命令回答「**要做**哪件事」，可以排队、记录、撤销。命令是第 4 章的第 3 篇。

## 6. 小结

- 策略模式把一族可以互换的算法分别封装成类，上下文只依赖它们的接口；加一种算法是新增一个类，已有的代码不用改（本文：`switch` 版改 7 处，策略版 3 处，已有的类一个不动）。
- 算法之间往往是取舍：四种缩放策略从 0.97 ms 到 93.34 ms、从 41.99 dB 到 50.06 dB，所以要能按场合选、在运行时换。选择的逻辑放在上下文或客户那里，策略之间互不知道。
- 现代 C++ 有四种写法：虚函数、`std::function`、模板参数、`std::variant`。**调用开销取决于粒度**：每个像素调一次，不能内联的写法慢 18%～39%；把策略放大到整帧，虚函数和模板一样快。
- 标准库里的比较函数、删除器、分配器、哈希函数都是编译期的策略；空的策略类借助空基类优化不占空间。
- 没有状态的策略可以共用；有状态的策略（缓存、码率控制的缓冲区）每个上下文要有自己的一份。
- 集合封闭、由自己掌控时，`enum` + `switch` 或 `variant` 是合理的选择；要让别人扩展时才需要策略。

刷题里的重构挑战 [运费计算](/problems/refactor-shipping-strategy/) 就是本文的场景：一个按快递方式分支的函数，要加一种新方式并重构成策略，提交时除了跑用例，还会检查代码结构。

## 7. 练习

1. **辨析**：下面四个设计分别是策略、状态、模板方法还是桥接？说出理由。
   - (a) 播放列表有「顺序」「单曲循环」「随机」三种模式，`Playlist` 持有一个 `NextPolicy`，用户点按钮时换成另一个；`next()` 交给它决定下一首。
   - (b) 下载任务有「等待」「下载中」「暂停」「完成」，`DownloadTask` 持有当前的 `TaskState`；`Downloading::onFinished()` 会把任务切换到 `Done`。
   - (c) `Decoder` 基类的 `run()` 固定是「打开 → 循环读包解码 → 冲刷 → 关闭」，`H264Decoder`、`AacDecoder` 只重写其中的「解码一个包」。
   - (d) 播放器有主窗口、画中画两种视图，又有 OpenGL、Metal 两种渲染后端；视图持有一个 `RenderBackend`，两边都有各自的子类。
2. **动手**：给 `strategy_scaler.cpp` 加一个「区域平均」（Area）策略：缩小时，一个输出像素盖住源图上 rx × ry 的一块，把这块的像素全部平均。用它把 8×4 的测试图缩小到 3×2，和另外三种比较。加的时候，`Scaler` 接口哪里不合适？
3. **设计**：视频编码器的码率控制有三种：CQP（固定量化参数）、CBR（恒定码率）、CRF（恒定质量）。编码每一帧前要决定量化参数 QP（越大压得越狠），编码后知道这一帧实际花了多少比特。把码率控制设计成策略：接口有哪些函数？哪些策略有状态？写一个能运行的模拟（码率模型可以简化成：比特数 = 复杂度 × 40000 × 2^((26 − QP) / 6)）。
4. **找问题**：下面的程序给标清和高清视频各生成一张 4×2 的缩略图，两个 `Thumbnailer` 共用一个双线性策略以「节省内存」。高清那张不对，问题出在哪？怎么改？

   ```cpp
   // 《策略模式》：练习 4——找问题：两个缩略图生成器共用一个「带缓存」的策略对象，第二个出来的图不对
   #include <algorithm>
   #include <cmath>
   #include <cstdint>
   #include <cstdio>
   #include <memory>
   #include <vector>

   struct Plane {
       int w, h;
       std::vector<uint8_t> px;
       Plane(int w_, int h_) : w(w_), h(h_), px(static_cast<size_t>(w_) * h_) {}
       int at(int x, int y) const {
           x = std::clamp(x, 0, w - 1);
           y = std::clamp(y, 0, h - 1);
           return px[static_cast<size_t>(y) * w + x];
       }
   };

   class Scaler {
   public:
       virtual ~Scaler() = default;
       virtual Plane scale(const Plane& src, int w, int h) const = 0;
   };

   // 「优化版」双线性：每一列对应的源坐标和权重只算一次，存起来以后查表
   class CachedBilinear : public Scaler {
   public:
       Plane scale(const Plane& src, int w, int h) const override {
           if (static_cast<int>(col0_.size()) != w) {               // 输出宽度变了才重新算
               col0_.assign(static_cast<size_t>(w), 0);
               colf_.assign(static_cast<size_t>(w), 0);
               for (int x = 0; x < w; ++x) {
                   float sx = (x + 0.5f) * src.w / w - 0.5f;
                   col0_[static_cast<size_t>(x)] = static_cast<int>(std::floor(sx));
                   colf_[static_cast<size_t>(x)] = sx - std::floor(sx);
               }
           }
           Plane dst(w, h);
           for (int y = 0; y < h; ++y) {
               float sy = (y + 0.5f) * src.h / h - 0.5f;
               int y0 = static_cast<int>(std::floor(sy));
               float fy = sy - y0;
               for (int x = 0; x < w; ++x) {
                   int x0 = col0_[static_cast<size_t>(x)];
                   float fx = colf_[static_cast<size_t>(x)];
                   float top = src.at(x0, y0) * (1 - fx) + src.at(x0 + 1, y0) * fx;
                   float bot = src.at(x0, y0 + 1) * (1 - fx) + src.at(x0 + 1, y0 + 1) * fx;
                   dst.px[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(top * (1 - fy) + bot * fy + 0.5f);
               }
           }
           return dst;
       }

   private:
       mutable std::vector<int> col0_;
       mutable std::vector<float> colf_;
   };

   class Thumbnailer {
   public:
       Thumbnailer(int w, int h, std::shared_ptr<const Scaler> s) : w_(w), h_(h), scaler_(std::move(s)) {}
       Plane make(const Plane& frame) const { return scaler_->scale(frame, w_, h_); }
   private:
       int w_, h_;
       std::shared_ptr<const Scaler> scaler_;
   };

   Plane ramp(int w, int h) {                       // 从左到右 0 → 220 的渐变
       Plane p(w, h);
       for (int y = 0; y < h; ++y)
           for (int x = 0; x < w; ++x) p.px[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(220 * x / (w - 1));
       return p;
   }
   void show(const char* tag, const Plane& p) {
       std::printf("%s", tag);
       for (int x = 0; x < p.w; ++x) std::printf(" %3d", p.at(x, 0));
       std::printf("\n");
   }

   int main() {
       auto bilinear = std::make_shared<CachedBilinear>();          // 「省内存」：只建一个，大家共用
       Thumbnailer forSd(4, 2, bilinear), forHd(4, 2, bilinear);    // 都生成 4×2 的缩略图
       show("标清视频 8×4  →", forSd.make(ramp(8, 4)));
       show("高清视频 16×8 →", forHd.make(ramp(16, 8)));
       show("高清（新对象）→", CachedBilinear{}.scale(ramp(16, 8), 4, 2));
   }
   ```

   ```
   标清视频 8×4  →  16  78 141 204
   高清视频 16×8 →   7  37  66  95
   高清（新对象）→  22  81 139 198
   ```

<details>
<summary>点击查看答案</summary>

1. (a) **策略**：三种模式是平等的选项，由用户从外面换，`NextPolicy` 之间互不知道。(b) **状态**：`Downloading` 自己决定下一个状态是 `Done`，状态之间知道彼此，切换发生在状态内部。(c) **模板方法**：流程写死在基类里，子类通过继承只重写其中一步；换解码器要换一个子类，而不是给同一个对象换一个部件。(d) **桥接**：有两个独立变化的维度，视图和渲染后端各自有继承体系，不是「一个算法有几种做法」。

2. 加一个 `Area` 类和表里的一行就能用，但 `taps()` 写不出来：前三种算法读的像素数是固定的，区域平均缩小 2 倍时读 2×2，缩小 4 倍时读 4×4，和缩放比例有关。只好改接口，把比例传进去：`taps(float rx, float ry)`，已有的三个策略都要跟着改签名（它们忽略这两个参数）。这说明加策略不总是「零修改」：接口是按已有策略的需要设计的，新策略的需要超出了它，接口就得改。

   ```cpp
   // 《策略模式》：练习 2——加一个区域平均（Area）策略：它读多少个源像素取决于缩小倍数，接口里的 taps() 得改
   #include <algorithm>
   #include <cmath>
   #include <cstdint>
   #include <cstdio>
   #include <functional>
   #include <map>
   #include <memory>
   #include <string>
   #include <vector>

   struct Plane {                                   // 一个 8 位灰度平面（视频帧的 Y 分量）
       int w, h;
       std::vector<uint8_t> px;
       Plane(int w_, int h_) : w(w_), h(h_), px(static_cast<size_t>(w_) * h_) {}
       int at(int x, int y) const {                 // 越界时取边上的像素
           x = std::clamp(x, 0, w - 1);
           y = std::clamp(y, 0, h - 1);
           return px[static_cast<size_t>(y) * w + x];
       }
   };

   // 策略接口：一种缩放算法
   class Scaler {
   public:
       virtual ~Scaler() = default;
       virtual const char* name() const = 0;
       virtual int taps(float rx, float ry) const = 0;   // 每个输出像素最多读几个源像素；rx、ry 是源图和输出的尺寸比
       virtual Plane scale(const Plane& src, int w, int h) const = 0;
   };

   // 各个算法共用的外层循环：把输出坐标映射回源图，交给 kernel 取值
   template <class Kernel>
   Plane resample(const Plane& src, int w, int h, Kernel kernel) {
       Plane dst(w, h);
       float rx = static_cast<float>(src.w) / w, ry = static_cast<float>(src.h) / h;
       for (int y = 0; y < h; ++y)
           for (int x = 0; x < w; ++x) {
               float v = kernel(src, (x + 0.5f) * rx - 0.5f, (y + 0.5f) * ry - 0.5f);
               dst.px[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(std::clamp(v + 0.5f, 0.f, 255.f));
           }
       return dst;
   }

   class Nearest : public Scaler {
   public:
       const char* name() const override { return "nearest"; }
       int taps(float, float) const override { return 1; }
       Plane scale(const Plane& src, int w, int h) const override {
           return resample(src, w, h, [](const Plane& p, float x, float y) -> float {
               return p.at(static_cast<int>(std::floor(x + 0.5f)), static_cast<int>(std::floor(y + 0.5f)));
           });
       }
   };

   class Bilinear : public Scaler {
   public:
       const char* name() const override { return "bilinear"; }
       int taps(float, float) const override { return 4; }
       Plane scale(const Plane& src, int w, int h) const override {
           return resample(src, w, h, [](const Plane& p, float x, float y) {
               int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
               float fx = x - x0, fy = y - y0;
               float top = p.at(x0, y0) * (1 - fx) + p.at(x0 + 1, y0) * fx;
               float bot = p.at(x0, y0 + 1) * (1 - fx) + p.at(x0 + 1, y0 + 1) * fx;
               return top * (1 - fy) + bot * fy;
           });
       }
   };

   class Bicubic : public Scaler {
   public:
       const char* name() const override { return "bicubic"; }
       int taps(float, float) const override { return 16; }
       Plane scale(const Plane& src, int w, int h) const override {
           return resample(src, w, h, [](const Plane& p, float x, float y) {
               int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
               float fx = x - x0, fy = y - y0;
               float wx[4], wy[4], sum = 0;                // 横竖各 4 个权重，先算好
               for (int i = 0; i < 4; ++i) wx[i] = cubic(fx - (i - 1)), wy[i] = cubic(fy - (i - 1));
               for (int j = 0; j < 4; ++j)
                   for (int i = 0; i < 4; ++i) sum += p.at(x0 + i - 1, y0 + j - 1) * wx[i] * wy[j];
               return sum;
           });
       }

   private:
       static float cubic(float t) {                // Keys 三次卷积核，a = -0.5
           t = std::fabs(t);
           if (t < 1) return (1.5f * t - 2.5f) * t * t + 1;
           if (t < 2) return ((-0.5f * t + 2.5f) * t - 4) * t + 2;
           return 0;
       }
   };

   // 区域平均：缩小时，一个输出像素盖住源图上 rx × ry 的一块，把这块全部平均（放大时退化成最近邻）
   class Area : public Scaler {
   public:
       const char* name() const override { return "area"; }
       int taps(float rx, float ry) const override {
           return static_cast<int>(std::ceil(std::max(rx, 1.f)) * std::ceil(std::max(ry, 1.f)));
       }
       Plane scale(const Plane& src, int w, int h) const override {
           Plane dst(w, h);
           for (int y = 0; y < h; ++y)
               for (int x = 0; x < w; ++x) {
                   int x0 = x * src.w / w, x1 = std::max(x0 + 1, (x + 1) * src.w / w);
                   int y0 = y * src.h / h, y1 = std::max(y0 + 1, (y + 1) * src.h / h);
                   int sum = 0, n = (x1 - x0) * (y1 - y0);
                   for (int sy = y0; sy < y1; ++sy)
                       for (int sx = x0; sx < x1; ++sx) sum += src.at(sx, sy);
                   dst.px[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>((sum + n / 2) / n);
               }
           return dst;
       }
   };

   // 名字 → 策略：命令行、配置文件、界面下拉框都从这里取
   std::unique_ptr<Scaler> makeScaler(const std::string& name) {
       static const std::map<std::string, std::function<std::unique_ptr<Scaler>()>> table = {
           {"nearest", [] { return std::make_unique<Nearest>(); }},
           {"bilinear", [] { return std::make_unique<Bilinear>(); }},
           {"bicubic", [] { return std::make_unique<Bicubic>(); }},
           {"area", [] { return std::make_unique<Area>(); }},
       };
       auto it = table.find(name);
       return it != table.end() ? it->second() : std::make_unique<Bilinear>();
   }

   // 上下文：生成缩略图。它不知道有哪些算法，只知道 Scaler 接口
   class Thumbnailer {
   public:
       Thumbnailer(int w, int h, std::unique_ptr<Scaler> s) : w_(w), h_(h), scaler_(std::move(s)) {}
       void setScaler(std::unique_ptr<Scaler> s) { scaler_ = std::move(s); }
       const Scaler& scaler() const { return *scaler_; }
       Plane make(const Plane& frame) const { return scaler_->scale(frame, w_, h_); }

   private:
       int w_, h_;
       std::unique_ptr<Scaler> scaler_;
   };

   int main() {
       Plane frame(8, 4);                           // 一小块测试图：左暗右亮，中间一条竖直的亮线
       for (int y = 0; y < frame.h; ++y)
           for (int x = 0; x < frame.w; ++x)
               frame.px[static_cast<size_t>(y) * frame.w + x] = static_cast<uint8_t>(x == 4 ? 240 : 20 + x * 10);

       Thumbnailer thumb(3, 2, makeScaler("nearest"));     // 这次是缩小：8×4 → 3×2
       float rx = 8.f / 3, ry = 4.f / 2;
       for (std::string arg : {"nearest", "bilinear", "bicubic", "area"}) {
           thumb.setScaler(makeScaler(arg));        // 运行时换算法
           Plane big = thumb.make(frame);
           std::printf("%-8s taps=%-2d 第一行:", thumb.scaler().name(), thumb.scaler().taps(rx, ry));
           for (int x = 0; x < big.w; ++x) std::printf(" %3d", big.at(x, 0));
           std::printf("\n");
       }
   }
   ```

   ```
   nearest  taps=1  第一行:  30 240  80
   bilinear taps=4  第一行:  28 145  82
   bicubic  taps=16 第一行:  28 156  82
   area     taps=6  第一行:  25 110  80
   ```

   缩小时，nearest 正好取到了亮线（240），换一个尺寸可能又整条跳过；area 把盖住的 40、50、240 三列平均成 110，不会因为采样位置不同而忽亮忽暗。这也是 libswscale 有 `SWS_AREA` 的原因。

3. 接口要有两个函数：编码前的 `qpFor(complexity)` 和编码后的 `onEncoded(bits)`。CQP 和 CRF 只看当前这一帧，没有状态；CBR 要记住之前多花或少花了多少比特（虚拟缓冲区），是有状态的，所以每个编码器必须有自己的一份（用 `unique_ptr` 独占）。

   ```cpp
   // 《策略模式》：练习 3——编码器的码率控制做成策略：CQP、CBR、CRF 决定每一帧用多大的量化参数 QP
   #include <algorithm>
   #include <cmath>
   #include <cstdio>
   #include <memory>
   #include <vector>

   // 简化的码率模型：画面越复杂、QP 越小，这一帧的比特数越多；QP 每加 6，比特数大约减半
   int frameBits(double complexity, int qp) { return static_cast<int>(complexity * 40000 * std::pow(2.0, (26 - qp) / 6.0)); }

   class RateControl {                              // 策略接口
   public:
       virtual ~RateControl() = default;
       virtual const char* name() const = 0;
       virtual int qpFor(double complexity) = 0;    // 编码前：这一帧用多大的 QP
       virtual void onEncoded(int bits) { (void)bits; }   // 编码后：告诉策略实际花了多少比特
   };

   // 固定 QP：最简单，码率随画面起伏
   class Cqp : public RateControl {
   public:
       explicit Cqp(int qp) : qp_(qp) {}
       const char* name() const override { return "CQP"; }
       int qpFor(double) override { return qp_; }
   private:
       int qp_;
   };

   // 恒定码率：按复杂度估算让这一帧正好花掉预算的 QP；之前花超的记在缓冲区里，分几帧还回去（有状态）
   class Cbr : public RateControl {
   public:
       explicit Cbr(int bitsPerFrame) : target_(bitsPerFrame) {}
       const char* name() const override { return "CBR"; }
       int qpFor(double complexity) override {
           double want = std::max(target_ - buffer_ / 4.0, target_ * 0.25);
           return std::clamp(26 + static_cast<int>(std::lround(6 * std::log2(complexity * 40000 / want))), 10, 51);
       }
       void onEncoded(int bits) override { buffer_ += bits - target_; }
   private:
       int target_, buffer_ = 0;
   };

   // 恒定质量：复杂的画面人眼不容易看出损失，可以多量化一点
   class Crf : public RateControl {
   public:
       explicit Crf(int crf) : crf_(crf) {}
       const char* name() const override { return "CRF"; }
       int qpFor(double complexity) override { return crf_ + static_cast<int>(std::lround(3 * std::log2(complexity))); }
   private:
       int crf_;
   };

   class Encoder {                                  // 上下文
   public:
       explicit Encoder(std::unique_ptr<RateControl> rc) : rc_(std::move(rc)) {}
       void encode(const std::vector<double>& complexity) {
           long total = 0;
           int lo = 1 << 30, hi = 0;
           std::printf("%-4s QP:", rc_->name());
           for (double c : complexity) {
               int qp = rc_->qpFor(c);
               int bits = frameBits(c, qp);
               rc_->onEncoded(bits);
               total += bits;
               lo = std::min(lo, bits), hi = std::max(hi, bits);
               std::printf(" %2d", qp);
           }
           std::printf("   每帧 %3d～%3d kbit，共 %ld kbit\n", lo / 1000, hi / 1000, total / 1000);
       }
   private:
       std::unique_ptr<RateControl> rc_;
   };

   int main() {
       std::vector<double> scene = {1, 1, 1, 1, 4, 4, 4, 4, 1, 1, 1, 1};   // 中间 4 帧是复杂画面
       Encoder(std::make_unique<Cqp>(26)).encode(scene);
       Encoder(std::make_unique<Cbr>(80000)).encode(scene);
       Encoder(std::make_unique<Crf>(26)).encode(scene);
   }
   ```

   ```
   CQP  QP: 26 26 26 26 26 26 26 26 26 26 26 26   每帧  40～160 kbit，共 960 kbit
   CBR  QP: 20 20 20 20 32 32 32 32 20 20 20 20   每帧  80～ 80 kbit，共 960 kbit
   CRF  QP: 26 26 26 26 32 32 32 32 26 26 26 26   每帧  40～ 80 kbit，共 640 kbit
   ```

   同一段画面，CQP 的码率跟着画面起伏（40～160 kbit），直播推流时网络扛不住峰值；CBR 每帧都是 80 kbit，代价是简单画面浪费比特（QP 20）、复杂画面压得很狠（QP 32）；CRF 简单画面保持质量、复杂画面多压一点，总共只用了 640 kbit，适合存成文件。x264、x265 的 `--qp`、`--bitrate`、`--crf` 就是在选这三种策略。

4. `CachedBilinear` 有状态：它缓存了「每一列对应源图的哪个位置」，可缓存是否有效只看了**输出宽度**。标清先用，按源宽 8 算好了表；高清的输出宽度也是 4，于是直接用了这张表，只采到了 16 像素宽的源图的左半边（7～95，而正确的是 22～198）。两处要改：缓存的键要包含所有影响结果的输入（源宽度和输出宽度）；有状态的策略不要在上下文之间共用，`Thumbnailer` 改成用 `unique_ptr` 独占自己的策略。只改第一处也能得到正确的结果，但两个生成器交替使用时缓存会被反复冲掉，多线程下还有数据竞争。

   ```cpp
   // 《策略模式》：练习 4 的答案——缓存的键要包含所有输入（源宽度），并且每个上下文拥有自己的策略对象
   #include <algorithm>
   #include <cmath>
   #include <cstdint>
   #include <cstdio>
   #include <memory>
   #include <vector>

   struct Plane {
       int w, h;
       std::vector<uint8_t> px;
       Plane(int w_, int h_) : w(w_), h(h_), px(static_cast<size_t>(w_) * h_) {}
       int at(int x, int y) const {
           x = std::clamp(x, 0, w - 1);
           y = std::clamp(y, 0, h - 1);
           return px[static_cast<size_t>(y) * w + x];
       }
   };

   class Scaler {
   public:
       virtual ~Scaler() = default;
       virtual Plane scale(const Plane& src, int w, int h) const = 0;
   };

   // 「优化版」双线性：每一列对应的源坐标和权重只算一次，存起来以后查表
   class CachedBilinear : public Scaler {
   public:
       Plane scale(const Plane& src, int w, int h) const override {
           if (static_cast<int>(col0_.size()) != w || srcW_ != src.w) {   // 源宽度和输出宽度都要比较
               srcW_ = src.w;
               col0_.assign(static_cast<size_t>(w), 0);
               colf_.assign(static_cast<size_t>(w), 0);
               for (int x = 0; x < w; ++x) {
                   float sx = (x + 0.5f) * src.w / w - 0.5f;
                   col0_[static_cast<size_t>(x)] = static_cast<int>(std::floor(sx));
                   colf_[static_cast<size_t>(x)] = sx - std::floor(sx);
               }
           }
           Plane dst(w, h);
           for (int y = 0; y < h; ++y) {
               float sy = (y + 0.5f) * src.h / h - 0.5f;
               int y0 = static_cast<int>(std::floor(sy));
               float fy = sy - y0;
               for (int x = 0; x < w; ++x) {
                   int x0 = col0_[static_cast<size_t>(x)];
                   float fx = colf_[static_cast<size_t>(x)];
                   float top = src.at(x0, y0) * (1 - fx) + src.at(x0 + 1, y0) * fx;
                   float bot = src.at(x0, y0 + 1) * (1 - fx) + src.at(x0 + 1, y0 + 1) * fx;
                   dst.px[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(top * (1 - fy) + bot * fy + 0.5f);
               }
           }
           return dst;
       }

   private:
       mutable int srcW_ = 0;
       mutable std::vector<int> col0_;
       mutable std::vector<float> colf_;
   };

   class Thumbnailer {
   public:
       Thumbnailer(int w, int h, std::unique_ptr<const Scaler> s) : w_(w), h_(h), scaler_(std::move(s)) {}
       Plane make(const Plane& frame) const { return scaler_->scale(frame, w_, h_); }
   private:
       int w_, h_;
       std::unique_ptr<const Scaler> scaler_;      // 独占：策略里有缓存，就不要和别人共用
   };

   Plane ramp(int w, int h) {                       // 从左到右 0 → 220 的渐变
       Plane p(w, h);
       for (int y = 0; y < h; ++y)
           for (int x = 0; x < w; ++x) p.px[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(220 * x / (w - 1));
       return p;
   }
   void show(const char* tag, const Plane& p) {
       std::printf("%s", tag);
       for (int x = 0; x < p.w; ++x) std::printf(" %3d", p.at(x, 0));
       std::printf("\n");
   }

   int main() {
       Thumbnailer forSd(4, 2, std::make_unique<CachedBilinear>());
       Thumbnailer forHd(4, 2, std::make_unique<CachedBilinear>());
       show("标清视频 8×4  →", forSd.make(ramp(8, 4)));
       show("高清视频 16×8 →", forHd.make(ramp(16, 8)));
       show("高清（新对象）→", CachedBilinear{}.scale(ramp(16, 8), 4, 2));
   }
   ```

   ```
   标清视频 8×4  →  16  78 141 204
   高清视频 16×8 →  22  81 139 198
   高清（新对象）→  22  81 139 198
   ```

</details>
