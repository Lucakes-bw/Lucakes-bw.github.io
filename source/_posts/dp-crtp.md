---
title: CRTP 奇异递归模板
date: 2026-11-20 12:00:00
updated: 2026-11-20 12:00:00
categories:
  - 设计模式
  - C++惯用法
tags:
  - 设计模式
  - C++惯用法
  - CRTP
  - 静态多态
  - Mixin
  - 模板
order: 3
description: 音频 DSP 滤镜在百万采样点热循环里用虚函数逐点分派时，每个采样点都要经 vptr 查 vtable 间接跳转，不仅让对象多出 8 字节 vptr，更关键的是阻断了编译器内联与 SIMD 自动向量化。用 CRTP（Curiously Recurring Template Pattern，奇异递归模板）把基类写成 template <class Derived> class FilterBase，在编译期通过 static_cast<const Derived*>(this) 绑定子类实现零成本静态多态：Apple clang 21 -O2 实测处理 1000 万采样点（4000/帧 × 2500 帧，7 轮中位数），虚函数耗时 10.19 ms（sizeof=16 B），CRTP 降至 1.00 ms（提速 10.2 倍，sizeof=4 B）。文章还拆解了 CRTP 两大工程用途（编译期模板方法与 Mixin 能力注入：Counted<T>、Cloneable<Derived, Base>、Comparable<Derived> 及 std::enable_shared_from_this）、常见坑（写错模板参数、无公共基类）及 C++23 deducing this 的演进。
top_group_list: true
disableNunjucks: true
viz: [dp-crtp]
---

> **本文目标**：理解 **CRTP（Curiously Recurring Template Pattern，奇异递归模板）** 的编译期静态多态原理；掌握它最常用的两大工程场景（**编译期模板方法**与 **Mixin 能力注入**，并认出标准库里的 `std::enable_shared_from_this`）；避开写错模板参数与异构容器两大常见坑；了解 C++23 `deducing this` 如何简化 CRTP；通过实测量化虚函数与 CRTP 的性能与内存差异。
>
> **前置知识**：《模板方法模式》《原型模式》；熟悉 C++ 类模板与 `static_cast`。
>
> **编译环境**：`g++ / clang++ -std=c++17`，完整实验与压测代码见 `projects/dp-lab/31-crtp/`。

## 1. 问题：热循环里的虚函数，为什么成了性能瓶颈？

### 1.1 经典虚函数音频滤镜

音频处理管线里有一组 PCM 采样点滤镜（增益 `GainFilter`、软削波 `SoftClipFilter` 等）。用经典的 GoF 模板方法模式，我们很容易写出一个带虚函数的基类 `VirtualFilter`：基类负责遍历缓冲、限幅到 `[-1.0, 1.0]` 并统计峰值，子类只需重写 `applySample(float x)` 实现单点计算：

```cpp
// 《CRTP 奇异递归模板》：对照版——经典虚函数基类逐采样点处理音频，每采样点一次虚调用且对象带 vptr
#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

class VirtualFilter {
 public:
  virtual ~VirtualFilter() = default;
  virtual const char* name() const = 0;
  virtual float applySample(float x) const = 0;  // 热循环里每个采样点一次虚调用

  // 模板方法骨架：统计峰值并限幅到 [-1.0, 1.0]
  float processBuffer(std::vector<float>& pcm) const {
    float peak = 0.0f;
    for (float& s : pcm) {
      s = std::clamp(applySample(s), -1.0f, 1.0f);
      peak = std::max(peak, s < 0 ? -s : s);
    }
    return peak;
  }
};

class GainFilter final : public VirtualFilter {
 public:
  explicit GainFilter(float gain) : gain_(gain) {}
  const char* name() const override { return "Gain"; }
  float applySample(float x) const override { return x * gain_; }
 private:
  float gain_;
};

class SoftClipFilter final : public VirtualFilter {
 public:
  explicit SoftClipFilter(float drive) : drive_(drive) {}
  const char* name() const override { return "SoftClip"; }
  float applySample(float x) const override {
    float y = x * drive_;
    return y / (1.0f + (y < 0 ? -y : y));
  }
 private:
  float drive_;
};

int main() {
  std::vector<float> pcm = {0.2f, -0.5f, 0.8f, -0.9f};
  std::vector<std::unique_ptr<VirtualFilter>> chain;
  chain.push_back(std::make_unique<GainFilter>(1.5f));
  chain.push_back(std::make_unique<SoftClipFilter>(2.0f));

  for (const auto& f : chain) {
    float peak = f->processBuffer(pcm);
    std::printf("[%s] peak=%.3f first=%.3f\n", f->name(), peak, pcm[0]);
  }
  std::printf("sizeof(GainFilter)=%zu B (含 %zuB vptr + 4B float)\n",
              sizeof(GainFilter), sizeof(void*));
}
```

```text
[Gain] peak=1.000 first=0.300
[SoftClip] peak=0.667 first=0.375
sizeof(GainFilter)=16 B (含 8B vptr + 4B float)
```

### 1.2 虚调用的真实代价：阻断内联与向量化

上面代码写起来很舒服，但一旦放进每秒 48000 个采样点（甚至多声道上千万采样点）的音频热循环里，两个问题立刻暴露：

1. **优化屏障（最致命）**：现代 CPU 分支预测器很强，单次虚函数间接跳转本身只花一两个纳秒；真正伤性能的是**编译器在 `VirtualFilter::processBuffer()` 里看不到 `applySample()` 的函数体**。它既不能把 `x * gain_` 内联进循环，也不能把 4 个 `float` 打包成一条 SIMD（NEON / AVX）指令并行计算。
2. **对象体积膨胀**：`GainFilter` 真正的数据只有一个 4 字节的 `float gain_`，却因为带了 8 字节 `vptr` 和 4 字节尾填充，膨胀成了 **16 字节**（4 倍体积）。如果场景里是成千上万个轻量粒子、几何图元或 AST 小节点，这 8 字节 `vptr` 会直接吃掉大量缓存带宽。

<figure class="diagram">
<svg viewBox="0 0 700 176" role="img" aria-label="左边是虚函数运行时多态：每次循环都要读取对象头部 vptr、查 vtable、间接跳转，阻断内联与 SIMD 向量化；右边是 CRTP 编译期静态多态：基类模板在编译期直接展开为具体子类的内联指令，零 vptr 且可自动向量化"><g style="font-family:var(--mono)"><text x="168" y="20" text-anchor="middle" style="fill:var(--fg);font-size:12.5px;font-weight:700">虚函数（运行时多态）</text><rect x="18" y="36" width="110" height="58" rx="5" style="fill:var(--panel);stroke:var(--danger);stroke-width:1.4"/><text x="73" y="57" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">GainFilter</text><text x="73" y="74" text-anchor="middle" style="fill:var(--danger);font-size:10px">vptr(8B) + gain_</text><text x="73" y="88" text-anchor="middle" style="fill:var(--muted);font-size:9.5px">sizeof = 16 B</text><line x1="128" y1="65" x2="156" y2="65" style="stroke:var(--danger);stroke-width:1.5"/><rect x="156" y="36" width="76" height="58" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.3"/><text x="194" y="61" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">vtable</text><text x="194" y="79" text-anchor="middle" style="fill:var(--muted);font-size:10px">查槽位 [1]</text><line x1="232" y1="65" x2="256" y2="65" style="stroke:var(--danger);stroke-width:1.5"/><rect x="256" y="36" width="76" height="58" rx="5" style="fill:var(--panel);stroke:var(--danger);stroke-width:1.4"/><text x="294" y="61" text-anchor="middle" style="fill:var(--fg);font-size:10.5px;font-weight:700">applySample</text><text x="294" y="79" text-anchor="middle" style="fill:var(--danger);font-size:9.5px">间接跳转</text><text x="174" y="122" text-anchor="middle" style="fill:var(--danger);font-size:10.5px">每采样点一次虚调用：无法内联</text><text x="174" y="140" text-anchor="middle" style="fill:var(--muted);font-size:10px">无法自动生成 SIMD 向量指令</text><line x1="350" y1="16" x2="350" y2="160" style="stroke:var(--border-2);stroke-width:1;stroke-dasharray:4 4"/><text x="525" y="20" text-anchor="middle" style="fill:var(--fg);font-size:12.5px;font-weight:700">CRTP（编译期静态多态）</text><rect x="372" y="36" width="122" height="58" rx="5" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.5"/><text x="433" y="57" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">CrtpGain</text><text x="433" y="74" text-anchor="middle" style="fill:var(--accent);font-size:10px">仅 gain_(4B)</text><text x="433" y="88" text-anchor="middle" style="fill:var(--muted);font-size:9.5px">sizeof = 4 B（零 vptr）</text><line x1="494" y1="65" x2="532" y2="65" style="stroke:var(--accent);stroke-width:1.6"/><rect x="532" y="36" width="150" height="58" rx="5" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="607" y="57" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">编译期直接内联</text><text x="607" y="74" text-anchor="middle" style="fill:var(--accent);font-size:10px">pcm[i] * gain_</text><text x="607" y="88" text-anchor="middle" style="fill:var(--muted);font-size:9.5px">自动 SIMD（4 float/条）</text><text x="527" y="122" text-anchor="middle" style="fill:var(--accent);font-size:10.5px">static_cast 编译期决议，零运行开销</text><text x="527" y="140" text-anchor="middle" style="fill:var(--muted);font-size:10px">1000 万采样点提速 10.2×</text></g></svg>
</figure>

核心矛盾很清晰：**我们想把公共骨架复写在基类里，但又不想在热循环里为运行时多态买单。** 如果在编译期就已经知道要调用哪个滤镜，能不能让基类直接「看见」子类类型？答案就是 **CRTP**。

## 2. CRTP 奇异递归模板：原理与静态多态

### 2.1 什么是「奇异递归」？

CRTP（Curiously Recurring Template Pattern，由 Jim Coplien 在 1995 年命名）的核心结构只有一句话——**子类把自己的类型作为模板参数，传给自己的基类模板**：

```
class GainFilter : public FilterBase<GainFilter>
```

初看这行代码的人都会冒出一个疑问：**定义 `GainFilter` 的时候，`GainFilter` 还没定义完（它还是个不完整类型），怎么就能当模板参数传给 `FilterBase`？**

原因在于 C++ 模板的**延迟实例化（Lazy Instantiation）**规则：
- 编译器实例化 `FilterBase<GainFilter>` 的**类定义**时，只检查成员变量和成员函数**声明**——此时只要基类不按值持有 `Derived` 成员、也不在类定义里写 `sizeof(Derived)`，就不要求 `Derived` 是完整类型。
- 基类成员函数**函数体**里的 `static_cast<const Derived*>(this)->applySample(s)`，直到真正**被调用**时才实例化；而到调用那一刻，`GainFilter` 早就完整定义完毕了！

### 2.2 用 `FilterBase<Derived>` 重构音频滤镜

把第 1.1 节的 `VirtualFilter` 改成 CRTP 静态多态：去掉全部 `virtual`，基类 `FilterBase<Derived>` 在 `processBuffer()` 里把 `this` 静态强转成 `const Derived&` 再调 `applySample()`：

```cpp
// 《CRTP 奇异递归模板》：静态多态——FilterBase<Derived> 用 static_cast 在编译期绑定子类，零 vptr 且可完全内联
#include <algorithm>
#include <cstdio>
#include <vector>

template <class Derived>
class FilterBase {
 public:
  // 公共骨架写在基类，热循环内通过 static_cast 直接调用子类方法（编译期决议，可内联+向量化）
  float processBuffer(std::vector<float>& pcm) const {
    const auto& self = static_cast<const Derived&>(*this);
    float peak = 0.0f;
    for (float& s : pcm) {
      s = std::clamp(self.applySample(s), -1.0f, 1.0f);
      peak = std::max(peak, s < 0 ? -s : s);
    }
    return peak;
  }

  const char* filterName() const {
    return static_cast<const Derived*>(this)->name();
  }

 protected:
  FilterBase() = default;   // 防止外部裸实例化基类
  ~FilterBase() = default;  // 无需虚析构：不通过基类指针 delete
};

class GainFilter : public FilterBase<GainFilter> {
 public:
  explicit GainFilter(float gain) : gain_(gain) {}
  const char* name() const { return "Gain"; }
  float applySample(float x) const { return x * gain_; }
 private:
  float gain_;
};

class SoftClipFilter : public FilterBase<SoftClipFilter> {
 public:
  explicit SoftClipFilter(float drive) : drive_(drive) {}
  const char* name() const { return "SoftClip"; }
  float applySample(float x) const {
    float y = x * drive_;
    return y / (1.0f + (y < 0 ? -y : y));
  }
 private:
  float drive_;
};

// 通用算法用模板接收任意 FilterBase<D>
template <class D>
void runFilter(const FilterBase<D>& f, std::vector<float>& pcm) {
  float peak = f.processBuffer(pcm);
  std::printf("[%s] peak=%.3f first=%.3f\n", f.filterName(), peak, pcm[0]);
}

int main() {
  std::vector<float> pcm = {0.2f, -0.5f, 0.8f, -0.9f};
  GainFilter gain(1.5f);
  SoftClipFilter clip(2.0f);

  runFilter(gain, pcm);
  runFilter(clip, pcm);
  std::printf("sizeof(GainFilter)=%zu B (无 vptr，纯 float 大小)\n", sizeof(GainFilter));
}
```

```text
[Gain] peak=1.000 first=0.300
[SoftClip] peak=0.667 first=0.375
sizeof(GainFilter)=4 B (无 vptr，纯 float 大小)
```

留意三个细节：
- `~FilterBase()` 设为 `protected` 且**非虚**：因为 CRTP 不通过基类指针 `delete` 子类，`protected` 析构直接从编译期禁止误写 `delete basePtr;`；
- `FilterBase<Derived>` 本身没有成员变量，受益于 **EBO（Empty Base Optimization，空基类优化）**，`sizeof(GainFilter)` 精确等于 `sizeof(float) == 4 B`；
- 通用函数 `runFilter` 不再接收 `const VirtualFilter&`，而是改成模板 `template <class D> void runFilter(const FilterBase<D>& f, ...)`，在编译期为每种滤镜各生成一份零开销的特化代码。

### 2.3 UML 类图与交互演示

```uml
label: CRTP 静态多态的类图：GainFilter 与 SoftClipFilter 各自把自身作为模板参数传给 FilterBase<Derived>，编译期通过 static_cast 反向调用子类方法
classes:
  - { id: Base, name: 'FilterBase<Derived>', x: 210, y: 18, w: 280, attrs: [], ops: ['+ processBuffer(pcm) : float', '+ filterName() : const char*', '# ~FilterBase()'] }
  - { id: Gain, name: 'GainFilter', x: 40, y: 168, w: 230, attrs: ['- gain_ : float'], ops: ['+ name() : const char*', '+ applySample(x: float) : float'] }
  - { id: Clip, name: 'SoftClipFilter', x: 410, y: 168, w: 240, attrs: ['- drive_ : float'], ops: ['+ name() : const char*', '+ applySample(x: float) : float'] }
rels:
  - { from: Gain, to: Base, type: inherit, label: 'Derived = GainFilter' }
  - { from: Clip, to: Base, type: inherit, label: 'Derived = SoftClipFilter' }
```

下面的交互演示分步对比了「CRTP 编译期展开 + 向量化」「虚函数运行时查表」以及下一节 `Counted<T>` Mixin 的独立静态内存布局：

<div class="dsviz" data-viz="dp-crtp"></div>

## 3. 两大高频工程用途：编译期模板方法与 Mixin 能力注入

在真实的 C++ 代码库里，CRTP 绝不只是用来「替代虚函数提速」。它有两大最常见、最值回票价的工程用法：

### 3.1 用途一：编译期模板方法（Static Template Method）

第 2.2 节的 `FilterBase<Derived>` 就是典型代表——**公共算法骨架写在基类，差异步骤交给子类**，只是把 GoF 模板方法里的运行时虚钩子换成了编译期 `static_cast<Derived*>`。

它特别适合**高频小粒度计算**：音视频 DSP 滤镜、表达式模板（Eigen 矩阵库 `Eigen::MatrixBase<Derived>` 靠 CRTP 消除矩阵连加的临时对象）、网络协议编解码器、以及各类游戏引擎的组件遍历器。

### 3.2 用途二：Mixin 能力注入（无样板复用横切能力）

比静态多态更常用的是 **CRTP Mixin**：把一项正交能力（对象计数、自动 `clone()`、比较运算符派生、序列化等）封装成一个小模板基类，哪个类需要，继承一下就自动获得全套能力。

为什么普通继承做不到，而必须用 CRTP？以三个经典 Mixin 为例：

1. **对象存活计数 `Counted<Derived>`**：如果用普通非模板基类 `Counted`，`VideoClip` 和 `AudioClip` 会共用同一个 `static int alive_`，根本分不清各自活了几个；用了 CRTP 后，`Counted<VideoClip>` 和 `Counted<AudioClip>` 是两个完全独立的类，各自独享一份 `inline static int alive_`！
2. **自动深拷贝 `Cloneable<Derived, Base>`**：在《原型模式》里，每个子类都要手写一遍几乎一模一样的 `clone() { return std::make_unique<Concrete>(*this); }`。用 CRTP Mixin 继承抽象基类 `ClipBase`，只需写一次模板，所有子类零样板获得运行时多态的 `clone()`（这也是**CRTP 与虚函数混用**的典范，同样适用于访问者模式的 `accept()`）。
3. **自动派生比较运算符 `Comparable<Derived>`**：子类只写 `operator==` 和 `operator<`，基类用隐藏友元（Hidden Friend）自动生成 `!=`、`>`、`<=`、`>=`（C++17 时代非常常用，C++20 后可由 `<=>` 替代）。

```cpp
// 《CRTP 奇异递归模板》：Mixin 能力注入——对象存活计数 Counted<T> + 自动深拷贝 Cloneable<Derived, Base> + 比较运算符
#include <cstdio>
#include <memory>

// Mixin 1：按派生类独立统计存活对象数量（不同 T 实例化出各自独立的 static 计数器）
template <class Derived>
class Counted {
 public:
  static int alive() { return alive_; }
 protected:
  Counted() { ++alive_; }
  Counted(const Counted&) { ++alive_; }
  Counted(Counted&&) noexcept { ++alive_; }
  ~Counted() { --alive_; }
 private:
  inline static int alive_ = 0;  // C++17 inline static，每个 Derived 独享一份
};

// Mixin 2：结合运行时多态，自动生成 Prototype 模式的 clone()，省去每个子类重复写样板
struct ClipBase {
  virtual ~ClipBase() = default;
  virtual std::unique_ptr<ClipBase> clone() const = 0;
  virtual const char* kind() const = 0;
};

template <class Derived, class Base = ClipBase>
class Cloneable : public Base {
 public:
  std::unique_ptr<Base> clone() const override {
    return std::make_unique<Derived>(static_cast<const Derived&>(*this));
  }
};

// Mixin 3：只需实现 == 和 <，自动派生 !=、<=、>、>=
template <class Derived>
struct Comparable {
  friend bool operator!=(const Derived& a, const Derived& b) { return !(a == b); }
  friend bool operator>(const Derived& a, const Derived& b)  { return b < a; }
  friend bool operator<=(const Derived& a, const Derived& b) { return !(b < a); }
  friend bool operator>=(const Derived& a, const Derived& b) { return !(a < b); }
};

class VideoClip : public Cloneable<VideoClip>, public Counted<VideoClip>, public Comparable<VideoClip> {
 public:
  explicit VideoClip(int frames) : frames_(frames) {}
  const char* kind() const override { return "VideoClip"; }
  int frames() const { return frames_; }
  bool operator==(const VideoClip& o) const { return frames_ == o.frames_; }
  bool operator<(const VideoClip& o) const  { return frames_ < o.frames_; }
 private:
  int frames_;
};

class AudioClip : public Cloneable<AudioClip>, public Counted<AudioClip> {
 public:
  const char* kind() const override { return "AudioClip"; }
};

int main() {
  VideoClip v1(120), v2(240);
  AudioClip a1;
  auto v1Copy = v1.clone();  // 自动生成的 clone()，零手写样板
  std::printf("alive: Video=%d Audio=%d | clone=%s\n",
              VideoClip::alive(), AudioClip::alive(), v1Copy->kind());
  std::printf("Comparable: v1!=v2:%d v2>v1:%d v1<=v2:%d\n",
              v1 != v2, v2 > v1, v1 <= v2);
  v1Copy.reset();
  std::printf("释放 clone 后 Video alive=%d\n", VideoClip::alive());
}
```

```text
alive: Video=3 Audio=1 | clone=VideoClip
Comparable: v1!=v2:1 v2>v1:1 v1<=v2:1
释放 clone 后 Video alive=2
```

### 3.3 标准库里的 CRTP：`std::enable_shared_from_this<T>`

你可能早就用过 CRTP，只是没意识到它的名字——标准库里最著名的 CRTP 就是 **`std::enable_shared_from_this<T>`**：

- 异步网络/音视频回调里，对象常需要在成员函数内拿到指向自己的 `std::shared_ptr<AudioSession>`（不能直接写 `std::shared_ptr<AudioSession>(this)`，那会造出第二个控制块导致**双重释放**）。
- 只要写成 `class AudioSession : public std::enable_shared_from_this<AudioSession>`，基类内部藏一个 `std::weak_ptr<AudioSession>`，调用 `shared_from_this()` 时就能准确返回带具体派生类型的 `std::shared_ptr<AudioSession>`，与外部共享同一个控制块：

```cpp
// 《CRTP 奇异递归模板》：标准库里的 CRTP——std::enable_shared_from_this<T> 安全在异步回调中续命
#include <cstdio>
#include <functional>
#include <memory>

class AudioSession : public std::enable_shared_from_this<AudioSession> {
 public:
  explicit AudioSession(int id) : id_(id) {}

  // 返回一个持有自身的异步回调：必须用 shared_from_this()，绝不能用 shared_ptr<AudioSession>(this)
  std::function<void()> makeAsyncCallback() {
    auto self = shared_from_this();  // 复用已有控制块，use_count + 1
    return [self]() {
      std::printf("异步回调执行：session #%d (use_count=%ld)\n",
                  self->id_, self.use_count());
    };
  }
 private:
  int id_;
};

int main() {
  std::function<void()> cb;
  {
    auto s = std::make_shared<AudioSession>(42);
    std::printf("注册回调前 use_count=%ld\n", s.use_count());
    cb = s->makeAsyncCallback();
    std::printf("注册回调后 use_count=%ld\n", s.use_count());
  }  // 局部指针 s 离开作用域，但回调里的 self 保住了对象生命周期
  cb();
}
```

```text
注册回调前 use_count=1
注册回调后 use_count=2
异步回调执行：session #42 (use_count=1)
```

此外，C++20 的 `std::ranges::view_interface<Derived>` 也是标准库 CRTP 的经典范例：自定义视图只需提供 `begin()` / `end()`，继承它就能自动获得 `empty()`、`size()`、`front()`、`back()`、`operator[]` 等全套容器接口。

## 4. 常见坑、C++23 `deducing this` 与实测对比

### 4.1 常见坑 ①：复制粘贴写错模板参数 `Derived`

CRTP 最隐蔽的 bug 来自复制粘贴——从 `MuteFilter` 拷贝一份改写 `GainFilter` 时，忘了改继承列表里的模板参数：

<!-- run: off -->
```cpp
// 错误：GainFilter 继承了 FilterBase<MuteFilter>！
struct GainFilter : FilterBase<MuteFilter> { ... };
```

如果 `FilterBase` 的构造函数是 `protected`，这段代码**竟然能编译通过**！但一调用 `apply()`，`static_cast<const MuteFilter*>(this)` 就把 `GainFilter*` 强转成了毫不相干的 `MuteFilter*`，直接触发**未定义行为（UB）**。

**防呆惯用法**：把基类构造函数设为 `private`，再加一句 `friend Derived;`。这样只有真正的 `Derived` 自己能构造 `SafeFilterBase<Derived>`，写错参数的子类会在编译期直接报错：

```cpp
// 《CRTP 奇异递归模板》：防呆惯用法——用私有构造 + friend Derived，在编译期拦住写错模板参数
#include <cstdio>

template <class Derived>
class SafeFilterBase {
 public:
  float apply(float x) const {
    return static_cast<const Derived*>(this)->applySample(x);
  }
 private:
  SafeFilterBase() = default;  // 构造函数设为 private
  friend Derived;              // 只有真正的 Derived 才能构造 SafeFilterBase<Derived>
};

struct MuteFilter : SafeFilterBase<MuteFilter> {
  float applySample(float) const { return 0.0f; }
};

// 若误写为 struct GainFilter : SafeFilterBase<MuteFilter>，
// 因 SafeFilterBase<MuteFilter> 构造仅对 friend MuteFilter 开放，编译期直接报错！
struct GainFilter : SafeFilterBase<GainFilter> {
  float gain = 2.0f;
  float applySample(float x) const { return x * gain; }
};

int main() {
  MuteFilter m;
  GainFilter g;
  std::printf("Mute(0.5)=%.2f Gain(0.5)=%.2f\n", m.apply(0.5f), g.apply(0.5f));
}
```

```text
Mute(0.5)=0.00 Gain(0.5)=1.00
```

在 `projects/dp-lab/31-crtp/measure.sh` 里我们用 `-DTRIGGER_WRONG_BASE` 做了负向编译测试，确认错误写法会被编译器当场拒绝。

### 4.2 常见坑 ②：不同特化没有公共基类，不能放进同一个 `vector`

`FilterBase<GainFilter>` 和 `FilterBase<SoftClipFilter>` 在 C++ 类型系统里是**两个完全不相干的类**，因此你**无法**写出 `std::vector<FilterBase*>` 把它们装进同一条动态滤镜链。

工程上有三种标准解法，按「类型集合是否开放」和「拓扑是否固定」选型：

| 解法 | 写法 | 特点与适用场景 |
| --- | --- | --- |
| **`std::variant` + `std::visit`** | `std::vector<std::variant<GainFilter, SoftClipFilter>>` | 零堆分配、无 vptr；适合滤镜种类在编译期封闭的集合 |
| **`std::tuple` 静态管线** | `std::tuple<GainFilter, SoftClipFilter>` + 折叠表达式 | 整条链完全内联展开；适合管线顺序在编译期固定的场景 |
| **外层薄虚基类 + 内层 CRTP** | `template <class D> class FilterMixin : public IFilter` | 支持运行期动态加载插件；每帧 4000 采样点只虚调用 1 次 `processBlock()`，块内仍走 CRTP 静态展开 |

这第三种「粗粒度虚基类 + 细粒度 CRTP」正是大型音视频引擎与游戏引擎最常用的架构：对外保持 `std::vector<std::unique_ptr<IFilter>>` 的插件扩展性，对内把百万次采样循环留在 CRTP 模板里，鱼和熊掌兼得。

### 4.3 C++23 的终结者：`deducing this`（显式对象参数）

传统 CRTP 有两个令人烦躁的毛病：继承列表要重复写自己的类名（容易写错），基类里满屏都是啰嗦的 `static_cast<Derived*>(this)`。

C++23 引入了 **显式对象参数（Explicit Object Parameter，俗称 `deducing this`）**——成员函数的第一个参数可以直接写 `this auto&& self`，编译器自动推导出调用者的真实派生类型，**彻底告别模板参数与 `static_cast`**：

<!-- run: off -->
```cpp
// C++23 deducing this：基类不再需要模板参数 <Derived>，也不再需要 static_cast
struct FilterBase23 {
  float processBuffer(this const auto& self, std::vector<float>& pcm) {
    float peak = 0.0f;
    for (float& s : pcm) {
      s = std::clamp(self.applySample(s), -1.0f, 1.0f);
      peak = std::max(peak, s < 0 ? -s : s);
    }
    return peak;
  }
};
struct GainFilter23 : FilterBase23 {  // 无需写成 FilterBase23<GainFilter23>！
  float gain;
  float applySample(float x) const { return x * gain; }
};
```

不仅代码砍半、根除了写错 `Derived` 的可能，连 `const` / 非 `const` / 右值引用重载也能靠一个 `this auto&& self` 一网打尽。不过在 C++17 / C++20 存量项目及需要按派生类拥有独立 `static` 成员（如 `Counted<T>`）的场景里，经典 CRTP 依然不可或缺。

### 4.4 实测对比：虚函数逐采样点 vs CRTP 静态多态

用 `projects/dp-lab/31-crtp/bench.cpp` 实测处理 **1000 万个音频采样点**（每帧 4000 采样点 = 16 KB 驻留 L1 缓存 × 2500 帧，符合真实音频 DSP 块处理；Apple clang 21，`-O2`，7 轮中位数）：

<figure class="diagram">
<svg viewBox="0 0 680 148" role="img" aria-label="1000 万音频采样点实测对比：虚函数逐采样点耗时 10.19 ms、对象大小 16 B；CRTP 静态多态耗时 1.00 ms（快 10.2 倍）、对象大小 4 B（降至 25%）"><g style="font-family:var(--mono)"><text x="115" y="18" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">1000 万采样点耗时（Clang -O2）</text><rect x="115" y="30" width="210" height="20" rx="2" style="fill:var(--danger)"/><text x="331" y="45" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">10.19 ms</text><rect x="115" y="60" width="21" height="20" rx="2" style="fill:var(--accent)"/><text x="142" y="75" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">1.00 ms（快 10.2×）</text><text x="455" y="18" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">单个滤镜对象体积 sizeof</text><rect x="455" y="30" width="144" height="20" rx="2" style="fill:var(--danger)"/><text x="605" y="45" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">16 B</text><rect x="455" y="60" width="36" height="20" rx="2" style="fill:var(--accent)"/><text x="497" y="75" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">4 B（降至 25%）</text><text x="107" y="45" text-anchor="end" style="fill:var(--fg);font-size:11.5px">虚函数逐点</text><text x="107" y="75" text-anchor="end" style="fill:var(--fg);font-size:11.5px">CRTP 静态</text><text x="10" y="116" text-anchor="start" style="fill:var(--muted);font-size:10.5px">条件：4000 采样点/帧（16 KB，L1 友好）× 2500 帧 = 1000 万采样点，Gain(1.25) + 限幅 [-1, 1]</text><text x="10" y="134" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench.cpp：Apple clang 21，-O2，7 轮中位数；完整脚本见 projects/dp-lab/31-crtp/</text></g></svg>
</figure>

| 方案 | 1000 万采样点耗时 | 单采样点耗时 | `sizeof(Gain)` | 能否内联 / SIMD 向量化 |
| --- | :---: | :---: | :---: | :---: |
| **虚函数逐采样点** | 10.19 ms | 1.02 ns | 16 B（含 8B vptr） | 否（虚调用阻断优化） |
| **CRTP 静态多态** | **1.00 ms** | **0.10 ns** | **4 B（零 vptr）** | **是（完全内联 + NEON/AVX）** |

**10.2 倍提速**并非单纯省下了一次跳转，而是编译器把 `applySample(s)` 内联进循环后，触发了 SIMD 自动向量化，单条指令同时算 4 个 `float`；而对象体积从 16 B 降到 4 B，在密集对象数组里同样意义重大。

## 5. CRTP 与运行时多态的选型边界

| 维度 | CRTP（编译期静态多态） | 虚函数（运行时多态） |
| --- | --- | --- |
| **类型决议时机** | 编译期确定 | 运行期按配置、用户输入或动态库决定 |
| **异构容器** | 无公共基类（需配 `std::variant` 或外层薄虚基类） | 天然支持 `std::vector<std::unique_ptr<Base>>` |
| **性能与体积** | 零 vptr、可内联与 SIMD 向量化 | 每对象 +8B vptr，热循环内阻断内联 |
| **编译期代价** | 每个子类实例化一份模板代码（代码膨胀、报错信息较长） | 只编译一份基类实现，接口清晰、编译快 |
| **典型场景** | DSP/数值计算热循环、Mixin（`Counted`/`Cloneable`）、`enable_shared_from_this` | 插件系统、UI 控件树、运行期动态拼装的处理链 |

记住一条实战法则：**虚函数放「粗粒度边界」（比如按音频块 `processBlock` 分派，每 4000 采样点只虚调用 1 次），CRTP 放「细粒度内核」与 Mixin 复用**。不要为了炫技把所有虚函数都改成 CRTP——如果一个函数每秒只调用 100 次，虚函数带来的清晰接口和独立编译比省下 100 纳秒划算得多。

## 6. 小结

- **CRTP 本质**是 `class Derived : public Base<Derived>`：利用模板函数体的延迟实例化，让基类在编译期通过 `static_cast<Derived*>(this)` 静态绑定子类，做到零 `vptr`、可内联、可自动向量化。
- **两大高频用途**：① 编译期模板方法（如 `FilterBase<Derived>`，实测处理 1000 万音频采样点比逐点虚调用快 **10.2×**，对象体积降至 **25%**）；② Mixin 能力注入（`Counted<T>` 按派生类独享静态计数器、`Cloneable<Derived, Base>` 消除 `clone()` 样板、`std::enable_shared_from_this<T>`）。
- **避坑要点**：用**私有构造 + `friend Derived`**把「写错模板参数」拦截在编译期；需要异构容器时，将 CRTP 与外层粗粒度虚基类或 `std::variant` 组合使用。
- **现代演进**：C++23 `deducing this`（`this auto&& self`）能取代大多数静态多态 CRTP，让基类不再需要模板参数与 `static_cast`。

## 7. 练习

1. **辨析**：下面三个场景，分别更适合用 **CRTP 静态多态**、**经典虚函数**，还是 **「外层虚基类 + 内层 CRTP」组合**？
   - (a) 音频宿主软件支持用户在运行期通过 `.vst3` 动态库加载第三方滤镜插件，每帧音频块 1024 采样点；
   - (b) 数学库里的向量/矩阵表达式（`a + b * 2.0f`），希望完全消除临时对象并在内层循环自动 SIMD 向量化；
   - (c) 有 30 种不同的视频特效类都继承自 `IEffect`，每个类都要实现一模一样的 `clone()` 和 `accept(EffectVisitor&)`。
2. **设计/改写**：给第 3.2 节的 `Counted<Derived>` 增加一个统计项 `createdTotal()`，记录程序启动以来某类**累计构造过**多少个对象（包括已销毁的），同时保持 `alive()` 只统计当前存活数量。注意拷贝构造和移动构造都要正确计数。
3. **找坑**：下面的 `ShapeBase<Derived>` 想用 CRTP 做静态多态，但藏了**两个**经典 bug（一个在编译期就能埋雷，一个会在运行时触发未定义行为）。找出来并给出修复方案。

<!-- run: off -->
```cpp
// 《CRTP 奇异递归模板》练习 3（有 bug）：找出两处 CRTP 常见陷阱
template <class Derived>
class ShapeBase {
 public:
  double area() const { return static_cast<const Derived*>(this)->areaImpl(); }
  ~ShapeBase() = default;                           // 隐患 ①：public 非虚析构
 protected:
  ShapeBase() = default;                            // 隐患 ②：protected 无法防写错 Derived
};

struct Circle : ShapeBase<Circle> {
  double r = 2.0;
  double areaImpl() const { return 3.14 * r * r; }
};

struct Square : ShapeBase<Circle> {                 // 复制粘贴忘了改模板参数！
  double side = 3.0;
  double areaImpl() const { return side * side; }
};

int main() {
  ShapeBase<Circle>* p = new Circle();
  delete p;                                         // 踩坑 ①
  Square sq;
  double a = sq.area();                             // 踩坑 ②
}
```

<details>
<summary>点击查看答案</summary>

1. (a) **经典虚函数（块级分派）**：第三方 `.vst3` 插件在运行期动态加载，编译期根本不知道插件类型，必须用运行时多态；把虚调用放在 1024 采样点的 `processBlock()` 边界上，单次虚调用开销被摊薄 1024 倍，几乎为零。
   (b) **CRTP 静态多态**：表达式类型在编译期完全已知且属于高频内层循环，CRTP（表达式模板）可实现零临时对象 + 完全内联 + SIMD 向量化。
   (c) **外层虚基类 + 内层 CRTP Mixin 组合**：对外通过 `IEffect` 虚基类放进同一个 `vector`，中间插一层 `template <class D> class EffectMixin : public IEffect` 自动生成 `clone()` 和 `accept()`，彻底消除 30 份重复样板代码。

2. 在 `Counted<Derived>` 里新增 `inline static int total_ = 0;`，在默认构造、拷贝构造、移动构造里都执行 `++alive_; ++total_;`，析构函数里只执行 `--alive_;`，再提供 `static int createdTotal() { return total_; }` 即可。因为 `Counted<Derived>` 按每个派生类独立特化，`VideoClip::createdTotal()` 与 `AudioClip::createdTotal()` 天然互不干扰。

3. **两处 bug 及修复**：
   - **隐患 ①（`public` 非虚析构 → `delete p` 触发 UB）**：`ShapeBase<Circle>* p = new Circle(); delete p;` 通过非虚基类指针删除派生类对象属于未定义行为。**修复**：把 `~ShapeBase() = default;` 移到 `protected:`，从编译期禁止外部通过基类指针 `delete`（如果确实需要通过基类指针管理生命周期，就应该使用虚基类 + `virtual ~Base()`）。
   - **隐患 ②（`protected` 构造放过了写错模板参数的 `Square : ShapeBase<Circle>` → `sq.area()` 触发 UB）**：`sq.area()` 会把 `Square*` 强转成 `Circle*` 去读 `Circle::r`，导致未定义行为。**修复**：把 `ShapeBase() = default;` 移到 `private:` 并加上 `friend Derived;`，`Square : ShapeBase<Circle>` 立刻会在编译期报错。

</details>
