---
title: 类型擦除（Type Erasure）
date: 2026-11-20 12:30:00
updated: 2026-11-20 12:30:00
categories:
  - 设计模式
  - C++惯用法
tags:
  - 设计模式
  - C++惯用法
  - 类型擦除
  - 值语义
  - 小对象优化
order: 4
description: 给音视频渲染管线组装异构图层（VideoLayer、SubtitleLayer、第三方 SdkWatermark）时，传统虚基类不仅强迫所有类侵入式继承 IDrawable、逼着为第三方类写适配器，还让容器退化成 std::vector<std::unique_ptr<IDrawable>>，丢失值语义并迫使每个子类手写 clone()。用 C++ 类型擦除（Type Erasure）重构：通过内部私有 Concept / Model<T> 把虚派发藏进值类型包装器 Drawable，让互不继承的独立结构体直接按值放进 std::vector<Drawable> 并天然支持拷贝；再进一步剖析标准库三大类型擦除组件（std::function、std::any、std::shared_ptr 删除器），并用 32 字节小对象优化（SBO）+ 手写静态 VTable 消除小对象堆分配。Apple clang 21 -O2 百万次实测：SBO 类型擦除堆分配从 100 万次降为 0 次，构造+调用耗时从 19.48 ns 降至 10.44 ns（快 1.87×），与 std::variant（7.09 ns）同属零堆分配梯队，同时保持类型集合完全开放。
top_group_list: true
disableNunjucks: true
viz: [dp-type-erasure]
---

> **本文目标**：理解为什么传统虚基类会同时带来**侵入式继承**与**丢失值语义**两大痛点；掌握经典的 **Concept / Model 类型擦除**写法，手写一个约 55 行的值类型包装器 `Drawable`；看懂标准库里的三大类型擦除（`std::function`、`std::any`、`std::shared_ptr` 擦除删除器）；掌握 **SBO（小对象优化）+ 手写 VTable** 消除小对象堆分配的工程手法；通过实测数据在虚基类、类型擦除、`std::variant` 与编译期模板之间做出准确选型。
>
> **前置知识**：《策略模式》《原型模式》《CRTP》；熟悉 C++ 模板、虚函数与 `std::unique_ptr`。
>
> **编译环境**：`g++ / clang++ -std=c++17`，完整实验与压测代码见 `projects/dp-lab/32-type-erasure/`。

## 1. 问题：传统继承多态的两座大山

### 1.1 渲染管线里混装异构图层

视频合成器的渲染管线需要把不同种类的图层按顺序放进同一个 `Scene` 容器，每帧依次调用 `draw()`：自研的 `VideoLayer`、字幕 `SubtitleLayer`，以及第三方商业 SDK 提供的水印类 `SdkWatermark`（只给头文件和静态库，**无法修改源码**）。

按传统面向对象写法，必须先定义一个抽象基类 `IDrawable`，再把所有图层塞进 `std::vector<std::unique_ptr<IDrawable>>`：

```cpp
// 《类型擦除》：问题版——传统虚基类要求侵入式继承、第三方类被迫写适配器，且容器丢失值语义
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct IDrawable {
  virtual ~IDrawable() = default;
  virtual void draw() const = 0;
  virtual std::unique_ptr<IDrawable> clone() const = 0;  // 痛点 ①：想拷贝场景，每个子类都得手写 clone()
};

struct VideoLayer : IDrawable {                          // 痛点 ②：业务类被迫侵入式继承 IDrawable
  int w, h;
  VideoLayer(int w, int h) : w(w), h(h) {}
  void draw() const override { std::printf("  [Video] %dx%d\n", w, h); }
  std::unique_ptr<IDrawable> clone() const override { return std::make_unique<VideoLayer>(*this); }
};

struct SubtitleLayer : IDrawable {
  std::string text;
  explicit SubtitleLayer(std::string t) : text(std::move(t)) {}
  void draw() const override { std::printf("  [Subtitle] \"%s\"\n", text.c_str()); }
  std::unique_ptr<IDrawable> clone() const override { return std::make_unique<SubtitleLayer>(*this); }
};

struct SdkWatermark {                                    // 第三方 SDK 类型：无法修改源码
  std::string brand;
  double alpha;
  void render() const { std::printf("  [Watermark] %s (alpha=%.1f)\n", brand.c_str(), alpha); }
};

struct WatermarkAdapter : IDrawable {                    // 痛点 ③：每个第三方类都要写适配器壳子
  SdkWatermark wm;
  explicit WatermarkAdapter(SdkWatermark w) : wm(std::move(w)) {}
  void draw() const override { wm.render(); }
  std::unique_ptr<IDrawable> clone() const override { return std::make_unique<WatermarkAdapter>(*this); }
};

using Scene = std::vector<std::unique_ptr<IDrawable>>;   // 痛点 ④：被迫装指针，不能直接拷贝

Scene cloneScene(const Scene& src) {
  Scene dst;
  dst.reserve(src.size());
  for (const auto& item : src) dst.push_back(item->clone());
  return dst;
}

int main() {
  Scene scene;
  scene.push_back(std::make_unique<VideoLayer>(1920, 1080));
  scene.push_back(std::make_unique<SubtitleLayer>("Hello Type Erasure"));
  scene.push_back(std::make_unique<WatermarkAdapter>(SdkWatermark{"LuckyStudio", 0.6}));

  // Scene backup = scene;                               // 编译失败：unique_ptr 不可拷贝！
  Scene backup = cloneScene(scene);
  std::printf("原场景 (%zu 层)：\n", scene.size());
  for (const auto& layer : scene) layer->draw();
  std::printf("克隆场景 (%zu 层)：\n", backup.size());
  for (const auto& layer : backup) layer->draw();
}
```

```text
原场景 (3 层)：
  [Video] 1920x1080
  [Subtitle] "Hello Type Erasure"
  [Watermark] LuckyStudio (alpha=0.6)
克隆场景 (3 层)：
  [Video] 1920x1080
  [Subtitle] "Hello Type Erasure"
  [Watermark] LuckyStudio (alpha=0.6)
```

### 1.2 侵入式继承与指针语义的四个硬伤

这段代码虽然能跑，但把 C++ 继承多态的四个老毛病全暴露出来了：

1. **强侵入性**：`VideoLayer` 本来只是一个 8 字节的纯数据结构，一旦继承 `IDrawable`，立刻多出 8 字节虚表指针 `vptr`，且与渲染框架的基类强绑定；第三方类 `SdkWatermark` 改不了源码，只能为每个类型手写一层 `WatermarkAdapter`。
2. **丢失值语义**：容器被迫变成 `std::vector<std::unique_ptr<IDrawable>>`，调用方满屏 `make_unique` 和 `->`。想把场景存个快照 `Scene backup = scene;`，直接编译报错，只能倒逼所有子类实现 `clone()`（原型模式样板代码）。
3. **堆分配碎片化**：哪怕一个图层只有 8 字节，也必须单独 `new` 到堆上，指针满天飞，CPU 缓存局部性大打折扣。
4. **多接口组合爆炸**：如果某个图层既要能 `draw()`，又要能 `serialize()`、`hitTest()`，传统继承就会演变成菱形多重继承的噩梦。

<figure class="diagram">
<svg viewBox="0 0 700 182" role="img" aria-label="左边是传统继承多态：所有具体图层必须侵入式继承 IDrawable，容器只能装 unique_ptr 指针；右边是类型擦除：VideoLayer 等独立结构体互不继承，由值类型包装器 Drawable 在内部用 Concept/Model 隐藏多态，容器直接按值存放 Drawable"><g style="font-family:var(--mono)"><text x="168" y="20" text-anchor="middle" style="fill:var(--fg);font-size:12.5px;font-weight:700">传统虚基类（侵入式 + 指针语义）</text><rect x="85" y="34" width="166" height="32" rx="5" style="fill:var(--panel);stroke:var(--danger);stroke-width:1.4"/><text x="168" y="54" text-anchor="middle" style="fill:var(--danger);font-size:11px;font-weight:700">IDrawable (虚基类)</text><rect x="18" y="96" width="94" height="36" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="65" y="118" text-anchor="middle" style="fill:var(--fg);font-size:10.5px">VideoLayer</text><rect x="122" y="96" width="94" height="36" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="169" y="118" text-anchor="middle" style="fill:var(--fg);font-size:10.5px">Subtitle</text><rect x="226" y="96" width="96" height="36" rx="4" style="fill:var(--panel);stroke:var(--danger);stroke-width:1.2;stroke-dasharray:4 2"/><text x="274" y="118" text-anchor="middle" style="fill:var(--danger);font-size:10.5px">Adapter 壳子</text><line x1="65" y1="96" x2="135" y2="66" style="stroke:var(--danger);stroke-width:1.3"/><line x1="169" y1="96" x2="168" y2="66" style="stroke:var(--danger);stroke-width:1.3"/><line x1="274" y1="96" x2="205" y2="66" style="stroke:var(--danger);stroke-width:1.3"/><text x="168" y="158" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">vector&lt;unique_ptr&lt;IDrawable&gt;&gt; 不可直接拷贝</text><text x="522" y="20" text-anchor="middle" style="fill:var(--fg);font-size:12.5px;font-weight:700">类型擦除（非侵入 + 值语义）</text><rect x="430" y="34" width="184" height="34" rx="5" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.6"/><text x="522" y="55" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">Drawable（值包装器）</text><rect x="366" y="96" width="94" height="36" rx="4" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.2"/><text x="413" y="118" text-anchor="middle" style="fill:var(--fg);font-size:10.5px">VideoLayer</text><rect x="474" y="96" width="96" height="36" rx="4" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.2"/><text x="522" y="118" text-anchor="middle" style="fill:var(--fg);font-size:10.5px">Subtitle</text><rect x="584" y="96" width="98" height="36" rx="4" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.2"/><text x="633" y="118" text-anchor="middle" style="fill:var(--fg);font-size:10.5px">Watermark</text><line x1="413" y1="96" x2="480" y2="68" style="stroke:var(--accent);stroke-width:1.3;stroke-dasharray:3 2"/><line x1="522" y1="96" x2="522" y2="68" style="stroke:var(--accent);stroke-width:1.3;stroke-dasharray:3 2"/><line x1="633" y1="96" x2="564" y2="68" style="stroke:var(--accent);stroke-width:1.3;stroke-dasharray:3 2"/><text x="522" y="158" text-anchor="middle" style="fill:var(--accent);font-size:10.5px">vector&lt;Drawable&gt; 互不继承，天然可拷贝</text></g></svg>
</figure>

有没有办法同时拿到**运行时多态的开放性**（任意第三方类型随时接入）、**模板的非侵入性**（不用继承公共基类）和**普通对象的值语义**（直接装 `std::vector<Drawable>`、直接拷贝）？这就是 **类型擦除（Type Erasure）** 要解决的核心问题。

## 2. 类型擦除：把多态藏进值类型内部

### 2.1 核心思想：Concept / Model 惯用法

Sean Parent 在那场著名的 *Inheritance Is The Base Class of Evil* 演讲里，把 C++ 类型擦除的本质浓缩成一句话：**多态不应该强加在数据类型的继承树上，而应该封装在使用方定义的值包装器内部。**

做法只用到三块积木（合称 **External Polymorphism + Bridge + Value Semantics**）：

1. **对外的值包装器 `Drawable`**：普通非虚类，支持拷贝、移动和 `draw()`，供业务方按值放进容器。
2. **内部抽象接口 `Concept`**：`Drawable` 的私有嵌套虚基类，声明擦除后保留的操作（`doDraw()`、`clone()`）。
3. **内部模板适配器 `Model<T>`**：继承 `Concept`，把任意满足约定的具体类型 `T` 包起来，把虚调用转发给 `data_.draw()`。

因为 `Concept` 和 `Model<T>` 全部藏在 `Drawable` 的 `private` 区域里，**具体类型 `T` 根本不知道 `Concept` 的存在**——这就是「擦除」的含义：在构造 `Drawable(T x)` 的那一刻，编译器根据 `T` 生成对应的 `Model<T>`，随后把它向上转型成 `std::unique_ptr<const Concept>`，对外彻底抹掉 `T` 的具体类型信息，只留下「能 `draw()`、能 `clone()`」的能力契约。

### 2.2 手写一个 55 行的 `Drawable` 值包装器

来看完整实现——三个图层类完全独立，不继承任何基类，也不含任何 `virtual`：

```cpp
// 《类型擦除》：经典 Concept/Model 写法——手写 Drawable 值类型包装器，非侵入式放入 std::vector
#include <cstdio>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// 三个互不继承、毫无关系的独立结构体（无需修改第三方源码）
struct VideoLayer {
  int w, h;
  void draw() const { std::printf("  [Video] %dx%d\n", w, h); }
};

struct SubtitleLayer {
  std::string text;
  void draw() const { std::printf("  [Subtitle] \"%s\"\n", text.c_str()); }
};

struct Watermark {
  std::string brand;
  double alpha;
  void draw() const { std::printf("  [Watermark] %s (alpha=%.1f)\n", brand.c_str(), alpha); }
};

// 类型擦除值包装器：对外是普通值类型，对内隐藏 Concept / Model<T> 虚派发
class Drawable {
 public:
  template <typename T,
            typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, Drawable>>>
  Drawable(T x) : self_(std::make_unique<Model<std::decay_t<T>>>(std::move(x))) {}

  Drawable(const Drawable& other) : self_(other.self_->clone()) {}
  Drawable(Drawable&&) noexcept = default;
  Drawable& operator=(Drawable other) noexcept {  // copy-and-swap 统一拷贝与移动赋值
    self_.swap(other.self_);
    return *this;
  }

  void draw() const { self_->doDraw(); }

 private:
  struct Concept {
    virtual ~Concept() = default;
    virtual void doDraw() const = 0;
    virtual std::unique_ptr<const Concept> clone() const = 0;
  };

  template <typename T>
  struct Model final : Concept {
    T data_;
    explicit Model(T x) : data_(std::move(x)) {}
    void doDraw() const override { data_.draw(); }
    std::unique_ptr<const Concept> clone() const override {
      return std::make_unique<Model>(data_);
    }
  };

  std::unique_ptr<const Concept> self_;
};

int main() {
  std::vector<Drawable> scene;                    // 直接按值存储，无需 unique_ptr！
  scene.push_back(VideoLayer{1920, 1080});
  scene.push_back(SubtitleLayer{"Hello Type Erasure"});
  scene.push_back(Watermark{"LuckyStudio", 0.6});

  std::vector<Drawable> backup = scene;           // 天然支持值语义拷贝！
  scene.push_back(Watermark{"DRAFT", 0.3});

  std::printf("原场景 (%zu 层)：\n", scene.size());
  for (const auto& layer : scene) layer.draw();
  std::printf("快照场景 (%zu 层)：\n", backup.size());
  for (const auto& layer : backup) layer.draw();
}
```

```text
原场景 (4 层)：
  [Video] 1920x1080
  [Subtitle] "Hello Type Erasure"
  [Watermark] LuckyStudio (alpha=0.6)
  [Watermark] DRAFT (alpha=0.3)
快照场景 (3 层)：
  [Video] 1920x1080
  [Subtitle] "Hello Type Erasure"
  [Watermark] LuckyStudio (alpha=0.6)
```

短短 35 行 `Drawable` 类定义，一次性把第 1 节的痛点清扫干净：
- `VideoLayer`、`SubtitleLayer`、`Watermark` 保持纯数据结构体（零 `vptr`、零基类依赖）；
- `std::vector<Drawable> backup = scene;` 一行完成深拷贝，修改原 `scene` 完全不影响 `backup`；
- 未来接入新图层（甚至 `int` 或 lambda），只要满足约定就能直接塞进 `scene`，调用方零改动。

### 2.3 UML 类图与交互演示

```uml
label: 类型擦除的类图：对外只有值类型 Drawable；内部用私有虚接口 Concept 与模板子类 Model<T> 承接任意互不继承的具体类型 T
classes:
  - { id: Drawable, x: 20, y: 20, w: 235, attrs: ['- self_ : unique_ptr<const Concept>'], ops: ['+ Drawable<T>(x: T)', '+ Drawable(const Drawable&)', '+ draw() const'] }
  - { id: Concept, x: 310, y: 20, w: 205, stereotype: interface, ops: ['+ doDraw() const = 0', '+ clone() const : unique_ptr'] }
  - { id: Model, x: 310, y: 145, w: 205, attrs: ['- data_ : T'], ops: ['+ doDraw() const', '+ clone() const : unique_ptr'] }
  - { id: ThirdParty, x: 565, y: 145, w: 175, attrs: ['VideoLayer / Subtitle / ...'], ops: ['+ draw() const  (无基类)'] }
rels:
  - { from: Drawable, to: Concept, type: comp, label: '1' }
  - { from: Model, to: Concept, type: impl }
  - { from: Model, to: ThirdParty, type: comp, label: '按值持有 T' }
```

下面的交互演示展示了 `Concept / Model<T>` 包装入队、值语义深拷贝，以及第 4 节将要实现的 SBO 栈内联与大对象落堆内存布局：

<div class="dsviz" data-viz="dp-type-erasure"></div>

### 2.4 两个关键工程细节

1. **为什么模板构造函数必须加 `std::enable_if_t` 约束？**
   如果不写 `std::enable_if_t<!std::is_same_v<std::decay_t<T>, Drawable>>`，当你拷贝一个**非 `const` 左值**（`Drawable d2(d1);`）时，模板构造函数 `Drawable(T)` 会实例化出 `Drawable(Drawable&)`，比 `Drawable(const Drawable&)` 匹配度更高！结果拷贝构造被模板构造函数劫持，把 `Drawable` 自己套进 `Model<Drawable>`，引发灾难性套娃。C++20 可以把这行 `enable_if_t` 换成更清爽的 `requires (!std::same_as<std::remove_cvref_t<T>, Drawable>)`。
2. **第三方类的方法名不叫 `draw()` 怎么办？（ADL 自由函数定制点）**
   上面第 1 节的 `SdkWatermark` 方法名叫 `render()` 而不是 `draw()`。只要把 `Model<T>::doDraw()` 里的 `data_.draw()` 改成**非限定自由函数调用** `draw(data_)`，然后在第三方类型所在命名空间补一个 1 行的自由函数重载 `inline void draw(const SdkWatermark& w) { w.render(); }`，无需改第三方类一字节源码，也不用写任何适配器类！这就是 Sean Parent 推崇的「非侵入式定制点」。

## 3. 标准库里的类型擦除三剑客

你其实每天都在用类型擦除——C++ 标准库里有三个最经典的代表，各自擦除的对象不同：

| 标准库组件 | 擦除了什么类型 | 保留了什么能力（Concept） | 是否支持 SBO |
| --- | --- | --- | :---: |
| **`std::function<R(Args...)>`** | 函数指针、lambda、仿函数、`std::bind` | 按固定签名 `R(Args...)` 调用 + 拷贝/销毁 | 主流 STL 均支持（通常 16～32 B） |
| **`std::any`** | 任意可拷贝构造的值类型 `T` | 拷贝、移动、销毁 + `typeid`（取出需 `std::any_cast<T>`） | 主流 STL 均支持（通常 16～32 B） |
| **`std::shared_ptr<T>`** | **仅擦除删除器（Deleter）与分配器类型** | 归零时通过控制块虚函数调用自定义删除器 | 控制块固定在堆上 |

重点对比一下 **`std::shared_ptr<T>` 与 `std::unique_ptr<T, Deleter>`** 的设计分叉：
- `std::unique_ptr<FILE, decltype(&fclose)>` 把删除器放进了**模板参数**（零开销，不擦除），所以两种不同删除器的 `unique_ptr` 是**不同类型**，不能放进同一个 `vector`；
- `std::shared_ptr<FILE>` 无论传 lambda、函数指针还是自定义释放器，类型永远都是 `std::shared_ptr<FILE>`——删除器被类型擦除后藏进了堆上的控制块（`control block`）里，换来 ABI 统一和容器兼容。

下面一段代码把这三剑客放到同一个音视频管线里对比演示：

```cpp
// 《类型擦除》：标准库里的三大类型擦除——std::function、std::any、std::shared_ptr 擦除删除器
#include <any>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct Frame { int id; };
struct GainFilter {
  double db;
  void operator()(Frame& f) const { std::printf("  [Gain %.1fdB] frame #%d\n", db, f.id); }
};
void deinterlace(Frame& f) { std::printf("  [Deinterlace] frame #%d\n", f.id); }

int main() {
  // 1. std::function：把函数指针、仿函数、带捕获 lambda 擦除成统一签名
  std::vector<std::function<void(Frame&)>> pipeline;
  pipeline.push_back(deinterlace);
  pipeline.push_back(GainFilter{+3.5});
  pipeline.push_back([tag = std::string("HDR")](Frame& f) {
    std::printf("  [ToneMap %s] frame #%d\n", tag.c_str(), f.id);
  });
  Frame f{42};
  std::printf("1) std::function 滤镜链：\n");
  for (const auto& step : pipeline) step(f);

  // 2. std::any：擦除任意可拷贝属性类型，取出时靠 any_cast 校验类型安全
  std::vector<std::pair<const char*, std::any>> meta = {
      {"fps", 60}, {"codec", std::string("AV1")}, {"hdr", true}};
  std::printf("2) std::any 元数据：fps=%d codec=%s\n",
              std::any_cast<int>(meta[0].second),
              std::any_cast<std::string>(meta[1].second).c_str());

  // 3. std::shared_ptr：不同自定义删除器擦除后类型完全一致，可放进同一容器
  std::vector<std::shared_ptr<int>> buffers;
  buffers.emplace_back(new int(1080), [](int* p) { std::printf("  [Pool释放] %d\n", *p); delete p; });
  buffers.emplace_back(new int(4096), [](int* p) { std::printf("  [GPU释放]  %d\n", *p); delete p; });
  std::printf("3) std::shared_ptr 擦除删除器：\n");
  for (auto& b : buffers) b.reset();
}
```

```text
1) std::function 滤镜链：
  [Deinterlace] frame #42
  [Gain 3.5dB] frame #42
  [ToneMap HDR] frame #42
2) std::any 元数据：fps=60 codec=AV1
3) std::shared_ptr 擦除删除器：
  [Pool释放] 1080
  [GPU释放]  4096
```

## 4. 性能进阶：SBO 小对象优化 + 手写 VTable

### 4.1 基础版为什么还不够快？

第 2.2 节的 `Drawable` 虽然优雅，但每存进一个哪怕只有 8 字节的 `VideoLayer`，都要调用一次 `std::make_unique<Model<VideoLayer>>`：
1. **每次构造/拷贝都有堆分配**：在高频创建临时回调、帧级渲染指令的场景里，`malloc/free` 很快会成为瓶颈；
2. **两次内存跳转**：`vector` 里存的是指针 `self_`，`draw()` 时先跳到堆上的 `Model<T>`，再读 `Model<T>` 里的虚表指针，最后跳到函数体，极易触发 CPU Cache Miss。

工程上的标准解法就是 **`std::function` 和 `std::any` 都在用的 SBO（Small Buffer Optimization，小对象优化）+ 手写静态 VTable**：
- 在 `SboDrawable` 内部自带一块对齐的栈缓冲区 `alignas(std::max_align_t) std::byte buf_[32]` 和一个函数指针表指针 `const VTable* vptr_`（总共 `32 + 8 = 40 B`）；
- 编译期用 `if constexpr` 判断：`sizeof(T) <= 32` 且对齐合法且 `nothrow` 可移动的小对象，直接用 **placement `new`** 构造在 `buf_` 里——**0 次堆分配**；
- 超过 32 字节的大对象，自动退回 `new T(...)`，把 8 字节堆指针写进 `buf_`——调用方完全无感知！

### 4.2 32 字节 SBO + 手写静态 VTable 实现

```cpp
// 《类型擦除》：进阶优化——32 字节 SBO 栈缓冲区 + 手写 VTable 函数指针表，小对象 0 次堆分配
#include <array>
#include <cstddef>
#include <cstdio>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

class SboDrawable {
  static constexpr std::size_t kBufSize  = 32;
  static constexpr std::size_t kBufAlign = alignof(std::max_align_t);

  struct VTable {
    void (*draw)(const void*);
    void (*copy)(const void* src, void* dst);
    void (*move)(void* src, void* dst) noexcept;
    void (*destroy)(void*) noexcept;
    bool isSmall;
  };

  template <typename T>
  static constexpr bool kFitsInSbo =
      sizeof(T) <= kBufSize && alignof(T) <= kBufAlign &&
      std::is_nothrow_move_constructible_v<T>;

  template <typename T>
  static const VTable* vtableFor() noexcept {
    if constexpr (kFitsInSbo<T>) {
      static const VTable vt{
          [](const void* p) { static_cast<const T*>(p)->draw(); },
          [](const void* s, void* d) { ::new (d) T(*static_cast<const T*>(s)); },
          [](void* s, void* d) noexcept { ::new (d) T(std::move(*static_cast<T*>(s))); },
          [](void* p) noexcept { static_cast<T*>(p)->~T(); },
          true};
      return &vt;
    } else {
      static const VTable vt{
          [](const void* p) { (*static_cast<T* const*>(p))->draw(); },
          [](const void* s, void* d) { ::new (d) T*(new T(**static_cast<T* const*>(s))); },
          [](void* s, void* d) noexcept {
            auto*& sp = *static_cast<T**>(s);
            ::new (d) T*(sp);
            sp = nullptr;
          },
          [](void* p) noexcept { delete *static_cast<T**>(p); },
          false};
      return &vt;
    }
  }

  alignas(kBufAlign) std::byte buf_[kBufSize]{};
  const VTable* vptr_ = nullptr;

 public:
  template <typename T,
            typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, SboDrawable>>>
  SboDrawable(T x) : vptr_(vtableFor<std::decay_t<T>>()) {
    using U = std::decay_t<T>;
    if constexpr (kFitsInSbo<U>) ::new (buf_) U(std::move(x));
    else ::new (buf_) U*(new U(std::move(x)));
  }

  SboDrawable(const SboDrawable& o) : vptr_(o.vptr_) { if (vptr_) vptr_->copy(o.buf_, buf_); }
  SboDrawable(SboDrawable&& o) noexcept : vptr_(o.vptr_) {
    if (vptr_) { vptr_->move(o.buf_, buf_); vptr_->destroy(o.buf_); o.vptr_ = nullptr; }
  }
  SboDrawable& operator=(SboDrawable o) noexcept {
    if (vptr_) vptr_->destroy(buf_);
    vptr_ = o.vptr_;
    if (vptr_) { vptr_->move(o.buf_, buf_); vptr_->destroy(o.buf_); o.vptr_ = nullptr; }
    return *this;
  }
  ~SboDrawable() { if (vptr_) vptr_->destroy(buf_); }

  void draw() const { vptr_->draw(buf_); }
  bool isSmall() const noexcept { return vptr_ && vptr_->isSmall; }
};

struct VideoLayer { int w, h; void draw() const { std::printf("  [Video] %dx%d\n", w, h); } };
struct Watermark {
  const char* brand; double alpha;
  void draw() const { std::printf("  [Watermark] %s (alpha=%.1f)\n", brand, alpha); }
};
struct LutOverlay {                               // 64 B > 32 B：自动退回堆分配
  std::array<float, 16> matrix;
  void draw() const { std::printf("  [LutOverlay] 4x4 matrix (m0=%.2f)\n", matrix[0]); }
};

int main() {
  std::vector<SboDrawable> scene;
  scene.push_back(VideoLayer{1920, 1080});
  scene.push_back(Watermark{"LuckyStudio", 0.6});
  scene.push_back(LutOverlay{{1.25f}});

  std::printf("SBO 栈缓冲阈值 = 32 B\n");
  const char* names[] = {"VideoLayer (8 B)", "Watermark (16 B)", "LutOverlay (64 B)"};
  for (std::size_t i = 0; i < scene.size(); ++i) {
    std::printf("%-18s → %s\n", names[i], scene[i].isSmall() ? "SBO 栈内联 (0 次堆分配)" : "自动落堆");
    scene[i].draw();
  }
}
```

```text
SBO 栈缓冲阈值 = 32 B
VideoLayer (8 B)   → SBO 栈内联 (0 次堆分配)
  [Video] 1920x1080
Watermark (16 B)   → SBO 栈内联 (0 次堆分配)
  [Watermark] LuckyStudio (alpha=0.6)
LutOverlay (64 B)  → 自动落堆
  [LutOverlay] 4x4 matrix (m0=1.25)
```

> [!TIP]
> 为什么 `kFitsInSbo<T>` 还要检查 `std::is_nothrow_move_constructible_v<T>`？
> 因为 `SboDrawable` 的移动构造函数标了 `noexcept`（让 `std::vector` 扩容时可以安全移动而不是降级为拷贝）。如果把一个移动时可能抛异常的类型塞进 SBO 栈缓冲区，`vptr_->move()` 一旦抛异常就会直接触发 `std::terminate()`；让它自动落堆后，移动只需交换 8 字节指针，天然 `noexcept`。

### 4.3 实测对比：四种多态方案性能与堆分配

用 `projects/dp-lab/32-type-erasure/bench.cpp` 实测 **100 万个混合小图层**（伪随机类型序列，含 3 种 `<= 24 B` 图层，预分配 `vector` 缓冲以排除扩容干扰；Apple clang 21，`-O2`，7 轮中位数）：

<figure class="diagram">
<svg viewBox="0 0 680 184" role="img" aria-label="100 万个图层构造加遍历调用加析构对比：传统虚基类 unique_ptr 19.91 ns 且 100 万次堆分配；Concept/Model 19.48 ns 且 100 万次堆分配；SBO 加 VTable 10.44 ns 且 0 次堆分配；std::variant 7.09 ns 且 0 次堆分配"><g style="font-family:var(--mono)"><text x="165" y="18" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">100 万次「构造 + 调用 + 析构」单元素耗时（Clang -O2）</text><text x="157" y="45" text-anchor="end" style="fill:var(--fg);font-size:11px">传统虚基类 unique_ptr</text><rect x="165" y="31" width="240" height="18" rx="2" style="fill:var(--danger)"/><text x="412" y="44" text-anchor="start" style="fill:var(--danger);font-size:11px;font-weight:700">19.91 ns（100 万次堆分配）</text><text x="157" y="73" text-anchor="end" style="fill:var(--fg);font-size:11px">Concept/Model 擦除</text><rect x="165" y="59" width="235" height="18" rx="2" style="fill:var(--warn)"/><text x="407" y="72" text-anchor="start" style="fill:var(--warn);font-size:11px;font-weight:700">19.48 ns（100 万次堆分配）</text><text x="157" y="101" text-anchor="end" style="fill:var(--fg);font-size:11px">SBO + VTable 擦除</text><rect x="165" y="87" width="126" height="18" rx="2" style="fill:var(--accent)"/><text x="298" y="100" text-anchor="start" style="fill:var(--accent);font-size:11px;font-weight:700">10.44 ns（0 次堆分配，快 1.91×）</text><text x="157" y="129" text-anchor="end" style="fill:var(--fg);font-size:11px">std::variant 封闭集</text><rect x="165" y="115" width="85" height="18" rx="2" style="fill:var(--link)"/><text x="257" y="128" text-anchor="start" style="fill:var(--link);font-size:11px;font-weight:700">7.09 ns（0 次堆分配，快 2.81×）</text><text x="10" y="158" text-anchor="start" style="fill:var(--muted);font-size:10.5px">容器热循环纯派发（1024 元素池 × ~1000 轮）：四者均在 1.06～1.12 ns/次，差距主要来自堆分配与缓存局部性</text><text x="10" y="174" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench.cpp：Apple clang 21，-O2，7 轮中位数；完整脚本见 projects/dp-lab/32-type-erasure/</text></g></svg>
</figure>

| 方案 | 元素 `sizeof` | 100 万次堆分配 | 构造+调用+析构 | 容器热循环调用 | 类型集合 |
| --- | :---: | :---: | :---: | :---: | :---: |
| **传统虚基类 `unique_ptr`** | 8 B | 1,000,000 | 19.91 ns | 1.12 ns | 开放（侵入继承） |
| **Concept/Model 类型擦除** | 8 B | 1,000,000 | 19.48 ns | 1.06 ns | 开放（非侵入） |
| **SBO + VTable 类型擦除** | 40 B | **0** | **10.44 ns** | **1.06 ns** | **开放（非侵入）** |
| **`std::variant` 封闭集合** | 24 B | **0** | **7.09 ns** | 1.07 ns | 封闭（编译期列全） |

读出三条反直觉的实测结论：
1. **Concept/Model 类型擦除并不比传统虚基类慢**：两者都是每次 1 次 `new` + 1 次虚调用，耗时几乎完全一致（19.48 ns vs 19.91 ns），但类型擦除换来了**非侵入式扩展 + 完整值语义**。
2. **SBO 的真正威力在消除 `new/delete`**：把小对象放进 32 B 栈内缓冲后，百万次堆分配直降为 **0**，整体耗时砍掉将近一半（19.48 ns → 10.44 ns），代价仅是元素占位从 8 B 增至 40 B。
3. **热循环里四者单次调用都在 ~1.1 ns**：现代 CPU 分支预测器和 L1 缓存下，单次虚调用/函数指针跳转本身非常便宜；真正拉开数量级差距的是**堆分配次数**与**对象是否连续排布在内存里**。

## 5. 四种 C++ 多态方案选型指南

遇到「需要统一处理多种类型」时，按下面这张表四选一即可：

| 方案 | 侵入性 | 类型集合 | 运行时容器混装 | 典型适用场景 |
| --- | :---: | :---: | :---: | --- |
| **编译期模板 / CRTP** | 非侵入 / 半侵入 | 开放 | ❌（不同类型不能放同一 `vector`） | 极致零开销热路径、算法泛型（如 `std::sort`、矩阵表达式模板） |
| **`std::variant` + `std::visit`** | 非侵入 | **封闭** | ✅（零堆分配、极紧凑） | 类型固定且数量少（如 AST 节点、协议消息、状态机状态） |
| **类型擦除（Concept/Model 或 SBO）** | **非侵入** | **开放** | ✅（值语义，SBO 可零堆分配） | 回调/滤镜/插件/渲染图层、需要对接第三方类、强调值语义与可拷贝 |
| **传统虚基类 + 智能指针** | 侵入式 | 开放 | ✅（指针语义） | 稳定大型类层级、需要共享基类状态或生命周期极长的大型服务对象 |

一句话口诀：**类型在编译期已定且不混装 → 模板/CRTP；类型封闭但需混装 → `std::variant`；类型开放且要非侵入值语义 → 类型擦除（高频小对象上 SBO）；重型长生命周期组件树 → 传统虚基类。**

## 6. 小结

- **类型擦除的本质**是把运行时多态从「数据类的继承树」转移到「使用方的值类型包装器内部」：外部是普通值语义，内部用 `Concept / Model<T>` 或手写 `VTable` 隐藏具体类型。
- **三大收益**：彻底摆脱侵入式基类（轻松接第三方库）、恢复天然值语义（容器可直接拷贝，告别满屏 `unique_ptr` 与 `clone()`）、支持 ADL 自由函数定制点。
- **标准库范例**：`std::function` 擦除可调用对象、`std::any` 擦除任意值类型、`std::shared_ptr` 仅擦除删除器以保持指针类型统一。
- **性能优化**：给类型擦除加上 **32 B SBO + 手写静态 VTable** 后，小对象实现 0 次堆分配（Apple clang 21 `-O2` 百万次实测耗时从 19.48 ns 降至 10.44 ns），大对象自动透明落堆。

## 7. 练习

1. **辨析选型**：下面三个音视频引擎模块，分别最适合用「模板/CRTP」「`std::variant`」「类型擦除」还是「传统虚基类」？
   - (a) 网络协议层的 RTMP 信令包，协议规定只有 5 种固定包类型，需要放进同一个收发队列；
   - (b) 剪辑软件的自定义特效渲染管线，允许外部开发者注册任意第三方结构体作为滤镜，需要按值存入历史快照并支持 Ctrl+Z 拷贝；
   - (c) 像素格式转换内核（YUV420 / NV12 / RGBA），在 4K 60fps 每像素热循环里执行，希望编译器完全内联 SIMD 化。
2. **非侵入式适配**：假设第三方库里有一个不能修改源码的 `struct PdfPage { int index; void printPage() const; };`。如何在**不修改 `PdfPage` 且不写额外适配器类**的前提下，让第 2.2 节的 `Drawable` 直接接收 `PdfPage`？
3. **找坑**：下面这份精简版 `Drawable` 看起来很干净，但编译 `Drawable copy = d;`（拷贝一个非 `const` 左值 `Drawable`）时却在模板实例化里报错（或触发递归套娃）。找出原因并说明如何修复。

<!-- run: off -->
```cpp
// 《类型擦除》练习 3（有坑）：未约束的模板构造函数劫持非 const 左值拷贝
class Drawable {
 public:
  template <typename T>
  Drawable(T x) : self_(std::make_unique<Model<T>>(std::move(x))) {}  // 坑在这里！

  Drawable(const Drawable& other) : self_(other.self_->clone()) {}
  void draw() const { self_->doDraw(); }
 private:
  struct Concept {
    virtual ~Concept() = default;
    virtual void doDraw() const = 0;
    virtual std::unique_ptr<const Concept> clone() const = 0;
  };
  template <typename T>
  struct Model final : Concept {
    T data_;
    explicit Model(T x) : data_(std::move(x)) {}
    void doDraw() const override { data_.draw(); }
    std::unique_ptr<const Concept> clone() const override { return std::make_unique<Model>(data_); }
  };
  std::unique_ptr<const Concept> self_;
};

int main() {
  Drawable d(VideoLayer{1920, 1080});
  Drawable copy = d;  // d 是非 const Drawable&：命中哪个构造函数？
}
```

<details>
<summary>点击查看答案</summary>

1. (a) **`std::variant`**：只有 5 种封闭类型且需放进同一队列，零堆分配且编译期检查穷尽性；
   (b) **类型擦除（推荐带 SBO）**：类型集合开放、需兼容第三方类，且历史快照要求天然值语义拷贝；
   (c) **编译期模板 / CRTP**：每像素热循环无法承受虚派发阻碍内联与自动向量化，编译期展开最优。

2. 把 `Drawable::Model<T>::doDraw()` 中的 `data_.draw()` 改为非限定自由函数调用 `draw(data_)`，并在 `PdfPage` 所在命名空间提供重载：
   ```cpp
   template <typename T>
   auto draw(const T& x) -> decltype(x.draw()) { x.draw(); }  // 默认转发给成员 draw()
   inline void draw(const PdfPage& p) { p.printPage(); }      // 为第三方类型定制
   ```
   这样 `scene.push_back(PdfPage{1});` 就能直接工作，零侵入、零适配器类。

3. **模板构造函数劫持拷贝构造**：`d` 的类型是**非 `const` 左值** `Drawable&`。重载决议时，拷贝构造函数 `Drawable(const Drawable&)` 需要额外加一层 `const` 转换，而模板构造函数 `template <typename T> Drawable(T)` 用 `T = Drawable`（若写成转发引用 `T&&` 则推导为 `Drawable&`）能精确匹配，从而抢走了拷贝构造！结果 `Model<Drawable>` 把一个 `Drawable` 又包了一层（如果是 `T&&` 转发引用更会直接无限递归或编译炸裂）。
   **修复**：给模板构造函数加上 SFINAE / Concept 约束，排除 `Drawable` 自身（及其 cv/引用修饰）：
   ```cpp
   template <typename T,
             typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, Drawable>>>
   Drawable(T x);
   ```

</details>
