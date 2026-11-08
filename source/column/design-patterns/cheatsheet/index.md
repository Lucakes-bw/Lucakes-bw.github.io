---
title: 设计模式速查
date: 2026-11-08 12:00:00
comments: false
description: 23 种 GoF 设计模式一页看完：每个模式一句话的意图、什么时候用、C++ 的写法要点和一张类图，开头附「按症状找模式」的对照表。
---

23 种 GoF 设计模式，每个一句话的意图、一张类图。类图的读法见《[UML 类图](/posts/dp-uml-class-diagram/)》；每个模式的完整讲解（问题、重构过程、现代 C++ 写法、实测和练习）在专栏正文里，写好一篇就在这里加上链接。

## 按症状找模式

先看代码哪里让你难受，再找对应的模式：

| 代码里的症状 | 可以考虑 |
| --- | --- |
| 一长串 `if (type == ...)` / `switch` 按类型选择做法，每加一种类型都要改它 | [策略](#p-strategy)、[状态](#p-state)、[工厂方法](#p-factory-method) |
| 对象的行为随内部状态变化，每个函数里都在判断当前状态 | [状态](#p-state) |
| 构造函数参数一大串，很多是可选的 | [建造者](#p-builder) |
| `new` 具体类散落在各处，换一个实现要改很多地方 | [工厂方法](#p-factory-method)、[抽象工厂](#p-abstract-factory) |
| 一个对象变化时，要手动通知一堆别的对象 | [观察者](#p-observer)、[中介者](#p-mediator) |
| 想支持撤销 / 重做，或者把操作排队、记录下来 | [命令](#p-command)、[备忘录](#p-memento) |
| 给类加功能导致子类数量爆炸（带缓冲的、带压缩的、带缓冲又带压缩的……） | [装饰器](#p-decorator)、[桥接](#p-bridge) |
| 第三方库或旧代码的接口和你要的对不上 | [适配器](#p-adapter) |
| 一个子系统要调好几个类、按固定顺序调用才能用 | [外观](#p-facade) |
| 树形结构（文件夹、界面控件、表达式），想把单个对象和一组对象一样对待 | [组合](#p-composite)、[解释器](#p-interpreter) |
| 几个算法的流程一样，只有其中几步不同 | [模板方法](#p-template-method) |
| 请求要经过一串处理者（鉴权、日志、限流……），每个都可能处理或放行 | [责任链](#p-chain) |
| 要对一个稳定的类层次加很多种新操作（打印、求值、优化……） | [访问者](#p-visitor) |
| 对象太大、创建太慢，或者访问前要做检查 | [代理](#p-proxy) |
| 成千上万个相似的小对象占了太多内存 | [享元](#p-flyweight) |
| 想复制一个对象，但只拿到了基类指针 | [原型](#p-prototype) |
| 整个程序只该有一个实例（日志、配置） | [单例](#p-singleton)（先想想能不能用依赖注入代替） |
| 想遍历自定义容器，又不想暴露内部结构 | [迭代器](#p-iterator) |

> 模式是用来解决问题的，不是用来套的。代码里没有上面这些症状时，直接写最简单的版本就好。

## 创建型模式

关心「对象怎么创建」：把创建的细节藏起来，让使用者不依赖具体的类。

<span id="p-singleton"></span>

### 单例 Singleton

**意图**：保证一个类只有一个实例，并提供一个全局访问点。
**什么时候用**：确实只能有一个的资源，比如进程级的日志、配置。
**C++ 要点**：函数内的 `static` 局部变量（Meyers 单例），C++11 保证初始化是线程安全的；删除拷贝构造和赋值。单例本质上是全局变量，会让测试变难，能用参数传进去就别用单例。

```uml
label: 单例：构造函数私有，静态函数 instance 返回唯一的实例
classes:
  - { id: Client, x: 20, y: 41, w: 100 }
  - { id: Logger, x: 230, y: 16, ops: ['$+ instance() : Logger&', '+ log(msg: string)', '- Logger()'] }
rels:
  - { from: Client, to: Logger, type: depend, label: 'instance()' }
notes:
  - { x: 470, y: 30, text: ['static Logger s;', 'return s;'], to: Logger }
```

<span id="p-factory-method"></span>

### 工厂方法 Factory Method

**意图**：定义一个创建对象的接口，让子类决定创建哪一个类。
**什么时候用**：框架里的通用流程需要一个对象，但具体是哪种对象由使用框架的人决定。
**C++ 要点**：虚函数返回 `std::unique_ptr<Product>`；简单的场合可以退化成一张「名字 → 创建函数」的注册表。

```uml
label: 工厂方法：Creator 的 operation 调用虚函数 create，具体创建哪种 Product 由子类 ConcreteCreator 决定
classes:
  - { id: Creator, stereo: abstract, x: 20, y: 16, w: 230, ops: ['+ operation()', '*# create() : unique_ptr<Product>'] }
  - { id: Product, stereo: interface, x: 400, y: 16, w: 150, ops: ['*+ use()'] }
  - { id: ConcreteCreator, x: 20, y: 170, w: 230, ops: ['# create() : unique_ptr<Product>'] }
  - { id: ConcreteProduct, x: 400, y: 170, w: 150, ops: ['+ use()'] }
rels:
  - { from: ConcreteCreator, to: Creator, type: inherit }
  - { from: ConcreteProduct, to: Product, type: realize }
  - { from: Creator, to: Product, type: depend }
  - { from: ConcreteCreator, to: ConcreteProduct, type: depend, label: '创建' }
```

<span id="p-abstract-factory"></span>

### 抽象工厂 Abstract Factory

**意图**：提供一个接口，用来创建一整族相关的对象，而不指定它们的具体类。
**什么时候用**：有多套成套的产品（Windows / macOS 风格的按钮、窗口……），同一时刻只用其中一套，并且不能混用。
**C++ 要点**：工厂接口的每个纯虚函数创建一种产品；程序启动时选好一个具体工厂，之后只通过接口使用。

```uml
label: 抽象工厂：GUIFactory 定义创建按钮和窗口的接口，WinFactory 和 MacFactory 各自创建一整套同风格的产品
classes:
  - { id: GUIFactory, stereo: interface, x: 90, y: 16, w: 190, ops: ['*+ createButton() : Button*', '*+ createWindow() : Window*'] }
  - { id: WinFactory, x: 20, y: 180, w: 120 }
  - { id: MacFactory, x: 230, y: 180, w: 120 }
  - { id: Button, stereo: interface, x: 460, y: 16, w: 110 }
  - { id: WinButton, x: 420, y: 180, w: 100 }
  - { id: MacButton, x: 540, y: 180, w: 100 }
rels:
  - { from: WinFactory, to: GUIFactory, type: realize, mid: 145 }
  - { from: MacFactory, to: GUIFactory, type: realize, mid: 145 }
  - { from: WinButton, to: Button, type: realize, mid: 145 }
  - { from: MacButton, to: Button, type: realize, mid: 145 }
  - { from: GUIFactory, to: Button, type: depend, fdx: -8 }
texts:
  - { x: 520, y: 248, text: 'Window 一族的结构和 Button 相同，图中省略', anchor: middle, muted: true }
```

<span id="p-builder"></span>

### 建造者 Builder

**意图**：把一个复杂对象的构造过程拆成一步一步，同样的步骤可以造出不同的表示。
**什么时候用**：构造函数参数太多、很多可选；或者对象要分好几步才能组装完整。
**C++ 要点**：每个设置函数返回 `*this` 的引用，写成链式调用；`build()` 最后检查必填项并返回成品。简单的情况用 C++20 的指定初始化（`{.url = ..., .timeout = 3}`）就够了。

```uml
label: 建造者：RequestBuilder 的设置函数都返回自身的引用，可以链式调用，最后 build 出一个 Request
classes:
  - { id: Client, x: 20, y: 54, w: 100 }
  - { id: RequestBuilder, x: 210, y: 16, ops: ['+ url(u: string) : RequestBuilder&', '+ header(k, v) : RequestBuilder&', '+ timeout(s: int) : RequestBuilder&', '+ build() : Request'] }
  - { id: Request, x: 560, y: 54, w: 100 }
rels:
  - { from: Client, to: RequestBuilder, type: depend }
  - { from: RequestBuilder, to: Request, type: depend, label: '创建' }
notes:
  - { x: 210, y: 150, text: 'RequestBuilder().url("/api").timeout(3).build()' }
```

<span id="p-prototype"></span>

### 原型 Prototype

**意图**：用一个已有的对象作为原型，通过复制它来创建新对象。
**什么时候用**：手里只有基类指针，却要复制出一个同样类型的对象（拷贝构造函数做不到，它不是虚函数）。
**C++ 要点**：虚函数 `clone()` 返回 `std::unique_ptr<Base>`，每个子类里写 `return std::make_unique<Derived>(*this);`，也可以用 CRTP 自动生成。

```uml
label: 原型：Shape 声明虚函数 clone，Circle 和 Rect 各自返回自己的副本；Client 只通过 Shape 指针复制对象
classes:
  - { id: Client, x: 20, y: 36, w: 100 }
  - { id: Shape, stereo: interface, x: 220, y: 16, w: 220, ops: ['*+ clone() : unique_ptr<Shape>'] }
  - { id: Circle, x: 150, y: 170, w: 160, ops: ['+ clone() : ...'] }
  - { id: Rect, x: 350, y: 170, w: 160, ops: ['+ clone() : ...'] }
rels:
  - { from: Client, to: Shape, type: assoc }
  - { from: Circle, to: Shape, type: realize, mid: 135 }
  - { from: Rect, to: Shape, type: realize, mid: 135 }
```

## 结构型模式

关心「类和对象怎么组合」：用组合代替继承，在不改原有代码的前提下拼出更大的结构。

<span id="p-adapter"></span>

### 适配器 Adapter

**意图**：把一个类的接口转换成使用者期望的另一个接口。
**什么时候用**：要用的库（常常是 C 库、旧代码）接口不对，又不能改它。
**C++ 要点**：适配器实现目标接口，内部持有被适配的对象（对象适配器，推荐）；也可以私有继承它（类适配器）。

```uml
label: 适配器：Client 只认识 Target 接口；Adapter 实现 Target，内部调用 LegacyLib 的 specificRequest
classes:
  - { id: Client, x: 20, y: 36, w: 100 }
  - { id: Target, stereo: interface, x: 210, y: 16, w: 150, ops: ['*+ request()'] }
  - { id: Adapter, x: 210, y: 170, w: 150, ops: ['+ request()'] }
  - { id: LegacyLib, x: 470, y: 170, w: 180, ops: ['+ specificRequest()'] }
rels:
  - { from: Client, to: Target, type: assoc }
  - { from: Adapter, to: Target, type: realize }
  - { from: Adapter, to: LegacyLib, type: compose }
```

<span id="p-bridge"></span>

### 桥接 Bridge

**意图**：把抽象部分和实现部分分开，让两者可以独立变化。
**什么时候用**：有两个独立变化的维度（形状 × 渲染后端），用继承会得到 M × N 个子类。
**C++ 要点**：抽象类持有实现接口的指针，两边各自继承；Pimpl 惯用法就是只有一种实现的桥接。

```uml
label: 桥接：Shape 持有一个 Renderer；Circle、Square 继承 Shape，OpenGL、Vulkan 实现 Renderer，两边独立扩展
classes:
  - { id: Shape, stereo: abstract, x: 70, y: 23, w: 170, ops: ['*+ draw()'] }
  - { id: Renderer, stereo: interface, x: 400, y: 16, w: 190, ops: ['*+ drawCircle(...)'] }
  - { id: Circle, x: 20, y: 170, w: 110 }
  - { id: Square, x: 180, y: 170, w: 110 }
  - { id: OpenGL, x: 370, y: 170, w: 110 }
  - { id: Vulkan, x: 510, y: 170, w: 110 }
rels:
  - { from: Shape, to: Renderer, type: aggregate, label: '桥' }
  - { from: Circle, to: Shape, type: inherit, mid: 125 }
  - { from: Square, to: Shape, type: inherit, mid: 125 }
  - { from: OpenGL, to: Renderer, type: realize, mid: 125 }
  - { from: Vulkan, to: Renderer, type: realize, mid: 125 }
```

<span id="p-composite"></span>

### 组合 Composite

**意图**：把对象组合成树形结构，让使用者用同样的方式对待单个对象和一组对象。
**什么时候用**：文件和文件夹、界面控件树、图形分组，凡是「整体和部分有同样的操作」的树。
**C++ 要点**：`Composite` 用 `std::vector<std::unique_ptr<Component>>` 管理子节点，操作时递归调用每个子节点。

```uml
label: 组合：Leaf 和 Composite 都实现 Component；Composite 还组合了多个 Component
classes:
  - { id: Component, stereo: interface, x: 160, y: 16, w: 170, ops: ['*+ size() : long'] }
  - { id: File, x: 40, y: 170, w: 130, ops: ['+ size() : long'] }
  - { id: Folder, x: 290, y: 170, w: 170, ops: ['+ add(c: unique_ptr<...>)', '+ size() : long'] }
rels:
  - { from: File, to: Component, type: realize, mid: 130, tdx: -20 }
  - { from: Folder, to: Component, type: realize, mid: 130, tdx: -20 }
  - { from: Folder, to: Component, key: kids, type: compose, tm: '*', points: [[430, 170], [430, 50], [330, 50]] }
```

<span id="p-decorator"></span>

### 装饰器 Decorator

**意图**：动态地给对象加上额外的职责，比生成子类更灵活。
**什么时候用**：功能可以自由叠加（缓冲、压缩、加密、计时……），每种组合都写一个子类不现实。
**C++ 要点**：装饰器和被装饰的对象实现同一个接口，内部持有 `std::unique_ptr<接口>`，先做自己的事再转发；可以一层套一层。

```uml
label: 装饰器：FileStream 和 Decorator 都实现 Stream；Decorator 内部持有一个 Stream；Gzip、Buffered 继承 Decorator，可以层层包裹
classes:
  - { id: Stream, stereo: interface, x: 180, y: 16, w: 170, ops: ['*+ write(data)'] }
  - { id: FileStream, x: 20, y: 170, w: 130 }
  - { id: Decorator, stereo: abstract, x: 300, y: 160, w: 160, attrs: ['- inner_ : unique_ptr<Stream>'] }
  - { id: Gzip, x: 250, y: 300, w: 100 }
  - { id: Buffered, x: 400, y: 300, w: 110 }
rels:
  - { from: FileStream, to: Stream, type: realize, mid: 120 }
  - { from: Decorator, to: Stream, type: realize, mid: 120, fdx: -40, tdx: 20 }
  - { from: Decorator, to: Stream, key: inner, type: compose, tm: '1', points: [[529, 187], [580, 187], [580, 50], [350, 50]] }
  - { from: Gzip, to: Decorator, type: inherit, mid: 270 }
  - { from: Buffered, to: Decorator, type: inherit, mid: 270 }
```

<span id="p-facade"></span>

### 外观 Facade

**意图**：给一个复杂的子系统提供一个简单的统一接口。
**什么时候用**：使用者只想「播放一个视频」，不想知道解封装、解码、同步、渲染的细节。
**C++ 要点**：外观类组合各个子系统对象，对外只暴露几个高层函数；它不阻止高级用户直接使用子系统。

```uml
label: 外观：Client 只调用 Player 的 play；Player 内部协调 Demuxer、Decoder、Renderer
classes:
  - { id: Client, x: 20, y: 58, w: 100 }
  - { id: Player, x: 210, y: 46, w: 150, ops: ['+ play(url)'] }
  - { id: Demuxer, x: 470, y: 10, w: 110 }
  - { id: Decoder, x: 470, y: 66, w: 110 }
  - { id: Renderer, x: 470, y: 122, w: 110 }
rels:
  - { from: Client, to: Player, type: depend }
  - { from: Player, to: Demuxer, type: compose, fdx: -14, mid: 420 }
  - { from: Player, to: Decoder, type: compose, fdx: 0, mid: 420 }
  - { from: Player, to: Renderer, type: compose, fdx: 14, mid: 420 }
```

<span id="p-flyweight"></span>

### 享元 Flyweight

**意图**：大量细粒度的对象共享相同的部分，以节省内存。
**什么时候用**：对象数量巨大（文字排版里的每个字符、游戏里的每颗子弹），并且大部分状态是重复的。
**C++ 要点**：把状态分成**内部状态**（可以共享，存在享元里，不可变）和**外部状态**（每次调用时传进来）；工厂用 `std::unordered_map` 缓存享元，返回 `std::shared_ptr<const Glyph>`。

```uml
label: 享元：GlyphFactory 缓存并共享 Glyph；Glyph 只保存字形这样的内部状态，位置作为外部状态在 draw 时传入
classes:
  - { id: Client, x: 20, y: 28, w: 100 }
  - { id: GlyphFactory, x: 200, y: 16, ops: ['+ get(ch: char) : shared_ptr<const Glyph>'] }
  - { id: Glyph, x: 237, y: 160, w: 240, attrs: ['- bitmap_  // 内部状态，共享'], ops: ['+ draw(x, y)  // 外部状态，传入'] }
rels:
  - { from: Client, to: GlyphFactory, type: depend }
  - { from: GlyphFactory, to: Glyph, type: aggregate, tm: '*', tdx: 0 }
```

<span id="p-proxy"></span>

### 代理 Proxy

**意图**：为另一个对象提供一个替身，以控制对它的访问。
**什么时候用**：延迟创建开销大的对象（虚代理）、访问前检查权限（保护代理）、访问远程对象（远程代理）、自动管理生命周期（智能指针）。
**C++ 要点**：代理和真实对象实现同一个接口；`std::unique_ptr` / `std::shared_ptr` 本身就是代理。

```uml
label: 代理：ImageProxy 和 RealImage 都实现 Image；ImageProxy 第一次 display 时才创建 RealImage
classes:
  - { id: Client, x: 20, y: 36, w: 100 }
  - { id: Image, stereo: interface, x: 220, y: 16, w: 150, ops: ['*+ display()'] }
  - { id: ImageProxy, x: 130, y: 170, w: 150, ops: ['+ display()'] }
  - { id: RealImage, x: 470, y: 170, w: 150, ops: ['+ display()'] }
rels:
  - { from: Client, to: Image, type: assoc }
  - { from: ImageProxy, to: Image, type: realize, mid: 125, tdx: -25 }
  - { from: RealImage, to: Image, type: realize, mid: 125, tdx: 25 }
  - { from: ImageProxy, to: RealImage, type: compose, tm: '0..1', label: '用到时才创建', lx: -16 }
```

## 行为型模式

关心「对象之间怎么分工、怎么通信」：把会变化的行为单独封装起来。

<span id="p-strategy"></span>

### 策略 Strategy

**意图**：把一族可以互相替换的算法分别封装起来，让使用者在运行时切换。
**什么时候用**：同一件事有多种做法，代码里出现了按类型选择做法的 `if-else` / `switch`。
**C++ 要点**：虚函数接口；只有一个函数的策略可以直接用 `std::function`；编译期就确定的策略用模板参数（比如 `std::sort` 的比较函数）。

```uml
label: 策略：Context 持有一个 Strategy，具体用哪一个算法可以在运行时替换
classes:
  - { id: Context, x: 20, y: 20, w: 200, attrs: ['- s_ : unique_ptr<Strategy>'], ops: ['+ setStrategy(s)'] }
  - { id: Strategy, stereo: interface, x: 360, y: 26, w: 150, ops: ['*+ execute()'] }
  - { id: QuickSort, x: 290, y: 170, w: 120 }
  - { id: MergeSort, x: 450, y: 170, w: 120 }
rels:
  - { from: Context, to: Strategy, type: compose, tm: '1' }
  - { from: QuickSort, to: Strategy, type: realize, mid: 135 }
  - { from: MergeSort, to: Strategy, type: realize, mid: 135 }
```

<span id="p-observer"></span>

### 观察者 Observer

**意图**：一个对象的状态变化时，自动通知所有依赖它的对象。
**什么时候用**：一处变化要触发多处更新，而且被通知的对象会增减（界面刷新、事件系统、Qt 的信号槽）。
**C++ 要点**：保存观察者时要考虑生命周期：用 `std::weak_ptr` 或在析构时取消订阅；通知过程中有观察者取消订阅也要处理好。

```uml
label: 观察者：Subject 聚合多个 Observer，状态变化时 notify 逐个调用 update
classes:
  - { id: Subject, x: 20, y: 16, w: 200, ops: ['+ attach(o: Observer*)', '+ detach(o: Observer*)', '+ notify()'] }
  - { id: Observer, stereo: interface, x: 380, y: 26, w: 150, ops: ['*+ update()'] }
  - { id: Screen, x: 310, y: 180, w: 110 }
  - { id: Logger, x: 460, y: 180, w: 110 }
rels:
  - { from: Subject, to: Observer, type: aggregate, tm: '*' }
  - { from: Screen, to: Observer, type: realize, mid: 140 }
  - { from: Logger, to: Observer, type: realize, mid: 140 }
```

```uml
label: 观察者的时序：setState 之后 Subject 调用 notify，在循环中依次调用每个观察者的 update
participants:
  - { id: m, name: main }
  - { id: s, name: 'subject: Subject' }
  - { id: a, name: 'screen: Screen' }
  - { id: b, name: 'log: Logger' }
messages:
  - { from: m, to: s, text: 'setState(42)', n: 1 }
  - { from: s, to: s, text: 'notify()', n: 2 }
  - { from: s, to: a, text: 'update()', n: 3 }
  - { from: a, to: s, type: reply }
  - { from: s, to: b, text: 'update()', n: 4 }
  - { from: b, to: s, type: reply }
  - { from: s, to: m, type: reply }
frags:
  - { from: 2, to: 5, label: 'loop [每个观察者]' }
```

<span id="p-command"></span>

### 命令 Command

**意图**：把一个请求封装成对象，从而可以排队、记录、撤销。
**什么时候用**：撤销 / 重做、宏、任务队列、菜单和快捷键绑定同一个操作。
**C++ 要点**：命令接口有 `execute()` 和 `undo()`；历史记录是 `std::vector<std::unique_ptr<Command>>`；不需要撤销的简单命令直接用 `std::function<void()>`。

```uml
label: 命令：Editor 保存命令的历史；InsertCmd 实现 Command，执行时操作接收者 Document
classes:
  - { id: Editor, x: 20, y: 16, w: 200, attrs: ['- history_ : vector<...>'], ops: ['+ run(c: unique_ptr<Command>)', '+ undo()'] }
  - { id: Command, stereo: interface, x: 380, y: 16, w: 150, ops: ['*+ execute()', '*+ undo()'] }
  - { id: InsertCmd, x: 380, y: 190, w: 150, ops: ['+ execute()', '+ undo()'] }
  - { id: Document, x: 100, y: 211, w: 120 }
rels:
  - { from: Editor, to: Command, type: compose, tm: '*', fdx: -5.5 }
  - { from: InsertCmd, to: Command, type: realize }
  - { from: InsertCmd, to: Document, type: assoc, label: '接收者' }
```

<span id="p-template-method"></span>

### 模板方法 Template Method

**意图**：在基类里定义算法的骨架，把其中某些步骤留给子类实现。
**什么时候用**：几个流程大体相同，只有个别步骤不同（测试框架的 setUp / run / tearDown、数据导出的打开 / 写入 / 关闭）。
**C++ 要点**：NVI（非虚接口）惯用法：公有的流程函数不是虚函数，被重写的步骤是 `private` 或 `protected` 的虚函数。

```uml
label: 模板方法：基类 Exporter 的 run 是固定的流程，依次调用 open、write、close 三个步骤，步骤由子类 CsvExporter 实现
classes:
  - { id: Exporter, stereo: abstract, x: 20, y: 16, w: 230, ops: ['+ run()', '*# open()', '*# write(row)', '*# close()'] }
  - { id: CsvExporter, x: 20, y: 200, w: 230, ops: ['# open()', '# write(row)', '# close()'] }
rels:
  - { from: CsvExporter, to: Exporter, type: inherit }
notes:
  - { x: 330, y: 30, text: ['void run() {      // 不是虚函数', '    open();', '    for (row : rows) write(row);', '    close();', '}'], to: Exporter }
```

<span id="p-iterator"></span>

### 迭代器 Iterator

**意图**：提供一种方法顺序访问容器中的元素，而不暴露容器的内部结构。
**什么时候用**：自己写的容器想支持 range-for 和 `<algorithm>`。
**C++ 要点**：STL 已经把这个模式做成了语言的一部分：容器提供 `begin()` / `end()`，迭代器实现 `*`、`++`、`!=`（以及 C++20 的 iterator concepts）。

```uml
label: 迭代器：RingBuffer 的 begin 和 end 创建 Iterator；Iterator 提供解引用、自增和比较
classes:
  - { id: RingBuffer, x: 20, y: 16, w: 190, ops: ['+ begin() : Iterator', '+ end() : Iterator'] }
  - { id: Iterator, x: 360, y: 16, w: 220, ops: ['+ operator*() : T&', '+ operator++() : Iterator&', '+ operator!=(o) : bool'] }
rels:
  - { from: RingBuffer, to: Iterator, type: depend, label: '创建', tdx: -8.5 }
notes:
  - { x: 20, y: 120, text: 'for (auto& x : buf) { ... }   // range-for 自动调用 begin / end' }
```

<span id="p-state"></span>

### 状态 State

**意图**：让一个对象在内部状态改变时改变它的行为，看起来像是换了一个类。
**什么时候用**：每个函数里都在 `switch (state_)`，状态和转换越来越多（连接、播放器、游戏角色）。
**C++ 要点**：每个状态一个类，由状态对象决定下一个状态；状态少而固定时，用 `std::variant` 加 `std::visit` 写状态机更简洁。

```uml
label: 状态：Connection 持有当前的 State，把操作委托给它；Closed、Listening、Established 三个状态类各自实现这些操作
classes:
  - { id: Connection, x: 20, y: 10, w: 210, attrs: ['- state_ : unique_ptr<State>'], ops: ['+ open()', '+ send(data)'] }
  - { id: State, stereo: interface, x: 380, y: 16, w: 170, ops: ['*+ open(c: Connection&)', '*+ send(c, data)'] }
  - { id: Closed, x: 260, y: 200, w: 100 }
  - { id: Listening, x: 400, y: 200, w: 100 }
  - { id: Established, x: 540, y: 200, w: 120 }
rels:
  - { from: Connection, to: State, type: compose, tm: '1' }
  - { from: Closed, to: State, type: realize, mid: 160 }
  - { from: Listening, to: State, type: realize, mid: 160 }
  - { from: Established, to: State, type: realize, mid: 160 }
```

<span id="p-chain"></span>

### 责任链 Chain of Responsibility

**意图**：让多个对象都有机会处理请求，把它们连成一条链，请求沿着链传递，直到有对象处理它。
**什么时候用**：HTTP 中间件、日志等级过滤、事件冒泡，处理者的组合和顺序经常变。
**C++ 要点**：基类保存下一个处理者的指针，默认实现是转发给下一个；也可以用 `std::vector<std::function<bool(Request&)>>` 依次调用，返回 `false` 时停止。

```uml
label: 责任链：Handler 保存下一个 Handler；Auth、RateLimit、Router 继承 Handler，处理不了就交给下一个
classes:
  - { id: Client, x: 20, y: 50, w: 100 }
  - { id: Handler, stereo: abstract, x: 210, y: 16, w: 230, attrs: ['- next_ : Handler*'], ops: ['+ setNext(h: Handler*)', '+ handle(req: Request&)'] }
  - { id: Auth, x: 140, y: 210, w: 100 }
  - { id: RateLimit, x: 275, y: 210, w: 100 }
  - { id: Router, x: 410, y: 210, w: 100 }
rels:
  - { from: Client, to: Handler, type: depend }
  - { from: Handler, to: Handler, key: next, type: assoc, tm: '0..1', label: 'next_', points: [[440, 60], [500, 60], [500, 100], [440, 100]] }
  - { from: Auth, to: Handler, type: inherit, mid: 175 }
  - { from: RateLimit, to: Handler, type: inherit, mid: 175 }
  - { from: Router, to: Handler, type: inherit, mid: 175 }
```

<span id="p-visitor"></span>

### 访问者 Visitor

**意图**：在不改变元素类的前提下，为一个类层次增加新的操作。
**什么时候用**：类层次很稳定（语法树的节点类型），但要不断加新操作（打印、求值、类型检查、优化）。
**C++ 要点**：经典写法是双分派：`accept(Visitor& v) { v.visit(*this); }`；类型集合固定时，`std::variant` 加 `std::visit` 是更现代的写法。

```uml
label: 访问者：Node 的 accept 接收一个 Visitor 并调用它的 visit；Printer 和Evaluator 是两种操作，各自实现对每种节点的 visit
classes:
  - { id: Node, stereo: interface, x: 20, y: 16, w: 190, ops: ['*+ accept(v: Visitor&)'] }
  - { id: Num, x: 20, y: 180, w: 80 }
  - { id: Add, x: 130, y: 180, w: 80 }
  - { id: Visitor, stereo: interface, x: 370, y: 16, w: 160, ops: ['*+ visit(n: Num&)', '*+ visit(a: Add&)'] }
  - { id: Printer, x: 330, y: 190, w: 100 }
  - { id: Evaluator, x: 460, y: 190, w: 110 }
rels:
  - { from: Num, to: Node, type: realize, mid: 140 }
  - { from: Add, to: Node, type: realize, mid: 140 }
  - { from: Node, to: Visitor, type: depend, tdx: -8.5 }
  - { from: Printer, to: Visitor, type: realize, mid: 150 }
  - { from: Evaluator, to: Visitor, type: realize, mid: 150 }
```

<span id="p-mediator"></span>

### 中介者 Mediator

**意图**：用一个中介对象封装一组对象之间的交互，让它们不直接互相引用。
**什么时候用**：一群对象两两互相调用，关系是一张网（对话框里联动的控件、聊天室里的用户）。
**C++ 要点**：同事对象只保存中介者的指针，所有交互都经过它；小心中介者自己变成什么都管的「上帝类」。

```uml
label: 中介者：User 之间不直接通信，都通过 ChatRoom 转发；ChatRoom 聚合多个 User
classes:
  - { id: ChatRoom, x: 20, y: 30, w: 210, ops: ['+ join(u: User*)', '+ send(from, msg)'] }
  - { id: User, x: 420, y: 30, w: 170, ops: ['+ say(msg)', '+ receive(msg)'] }
rels:
  - { from: ChatRoom, to: User, type: aggregate, tm: '*', fdx: -12, tdx: -12 }
  - { from: User, to: ChatRoom, type: assoc, fdx: 12, tdx: 12, label: 'room_' }
```

<span id="p-memento"></span>

### 备忘录 Memento

**意图**：在不破坏封装的前提下，保存一个对象的内部状态，以后可以恢复。
**什么时候用**：撤销、快照、存档。
**C++ 要点**：备忘录的内容只有原发器能读（把原发器声明为 `friend`，或者把备忘录做成它的私有嵌套类）；状态很大时考虑只存差异。

```uml
label: 备忘录：Editor 创建 Memento 保存自己的状态，也能从 Memento 恢复；History 保存一串 Memento，但看不到里面的内容
classes:
  - { id: Editor, x: 20, y: 16, w: 210, attrs: ['- text_ : string'], ops: ['+ save() : Memento', '+ restore(m: Memento)'] }
  - { id: Memento, x: 380, y: 40, w: 150, attrs: ['- text_ : string'] }
  - { id: History, x: 380, y: 190, w: 150, ops: ['+ push(m)', '+ pop() : Memento'] }
rels:
  - { from: Editor, to: Memento, type: depend, label: '创建 / 读取', fdx: 3 }
  - { from: History, to: Memento, type: compose, tm: '*' }
```

<span id="p-interpreter"></span>

### 解释器 Interpreter

**意图**：为一门简单的语言定义文法，并用一组类来表示和执行这门语言的句子。
**什么时候用**：规则引擎、简单的表达式和过滤条件。复杂的语言应该用解析器生成工具，而不是手写一堆类。
**C++ 要点**：每条文法规则一个类，结构和组合模式相同；求值就是在语法树上递归调用 `eval`。

```uml
label: 解释器：Expr 是表达式接口；Num 是终结符；Add、Mul 组合了左右两个子表达式
classes:
  - { id: Expr, stereo: interface, x: 170, y: 16, w: 190, ops: ['*+ eval(ctx) : int'] }
  - { id: Num, x: 20, y: 180, w: 110, attrs: ['- v_ : int'] }
  - { id: Add, x: 160, y: 180, w: 130, attrs: ['- l_, r_ : unique_ptr<Expr>'] }
  - { id: Mul, x: 420, y: 180, w: 110 }
rels:
  - { from: Num, to: Expr, type: realize, mid: 135, tdx: -20 }
  - { from: Add, to: Expr, type: realize, mid: 135, tdx: -20 }
  - { from: Mul, to: Expr, type: realize, mid: 135, tdx: -20 }
  - { from: Add, to: Expr, key: kids, type: compose, tm: '2', points: [[374, 207], [395, 207], [395, 50], [360, 50]] }
```

## 怎么记

- **创建型**问「谁来 `new`」：单例只 new 一次，工厂把 new 藏进函数，抽象工厂成套地 new，建造者分步 new，原型照着样子 new。
- **结构型**问「怎么包一层」：适配器转接口，装饰器加功能，代理控制访问，外观简化接口；桥接和组合是把对象拼成更大的结构。
- **行为型**问「谁调用谁」：策略、状态、命令把行为变成对象；观察者、中介者管通知；模板方法、责任链、访问者、迭代器、解释器决定调用的顺序和路线；备忘录保存行为的结果。
