---
title: 备忘录模式
date: 2026-11-20 10:00:00
updated: 2026-11-20 10:00:00
categories:
  - 设计模式
  - 行为型模式
tags:
  - 设计模式
  - 备忘录模式
  - 撤销
  - 封装
  - 写时复制
order: 10
description: 给视频调色台 ColorGrader 加撤销功能时，如果把亮度、对比度、LUT 名称、GPU 缓存脏标记 cacheDirty 和内部校验和 checksum 全部暴露给外部历史栈，不仅会漏存私有状态导致画面不刷新，还会让外部代码绕过边界校验写坏内部不变式。用备忘录模式重构：ColorGrader::Snapshot 构造和字段全私有，只对 friend class ColorGrader 开放宽接口，对 History（Caretaker）只暴露不透明值对象与只读 label()。面对 256 KB 3D LUT 大状态带来的内存爆炸，用现代 C++ std::shared_ptr<const LutTable> 实现写时复制（COW）共享备忘录：Apple clang 21 -O2 实测 1000 次存档（每 100 次切 LUT），整份深拷贝耗时 2538.8 ns/次、占 250.06 MB；COW 共享降至 3.6 ns/次（加速 708.6 倍）、仅占 2.53 MB（降至 1.0%）。最后对比备忘录与命令、原型、序列化。
top_group_list: true
disableNunjucks: true
viz: [dp-memento]
---

> **本文目标**：理解备忘录模式如何在**不破坏封装**的前提下保存与恢复对象内部状态；掌握 C++ 用私有嵌套类 + `friend` 实现**窄接口 / 宽接口**的惯用法；解决大状态快照的内存爆炸（COW 共享不可变大对象、有界历史栈、全量快照 + 增量命令混合）；通过实测对比深拷贝与 `std::shared_ptr<const T>` COW 备忘录的内存和耗时；分清备忘录与命令、原型、序列化。
>
> **前置知识**：《命令模式》《原型模式》；熟悉 C++ `friend` 与 `std::shared_ptr<const T>`。
>
> **编译环境**：`g++ / clang++ -std=c++17`，完整实验、压测与练习代码见 `projects/dp-lab/27-memento/`。

## 1. 问题：想加撤销，却被迫破坏封装

### 1.1 为了存档，把内部字段全敞开

视频剪辑软件里有一个调色台 `ColorGrader`，维护几项调色参数（亮度 `brightness` ∈ `[-100, 100]`、对比度 `contrast` ∈ `[0, 200]`、饱和度 `saturation` ∈ `[0, 200]`、LUT 滤镜名 `lutName`），以及两项**仅供内部使用**的私有状态：

- `cacheDirty`：GPU Shader Uniform 缓存脏标记。每渲染完一帧置 `false`；参数变化后置 `true`，通知下一帧重新上传参数。
- `checksum`：内部状态校验和（不变式：`checksum == brightness + contrast + saturation`），用于检测参数是否合法更新。

现在要给调色台加 **Ctrl+Z 撤销**。最直觉的做法是：把 `ColorGrader` 的字段全部改成 `public`（或补齐所有 getter/setter），让外部的 `History` 每次修改前把字段抄进 `RawBackup` 结构体，撤销时再逐个写回去：

```cpp
// 《备忘录模式》：问题版——为了做撤销把 ColorGrader 内部字段全部暴露，导致漏存私有状态且破坏不变式
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

struct ColorGrader {
  int brightness = 0, contrast = 100, saturation = 100;
  std::string lutName = "None";
  bool cacheDirty = true;                 // 内部私有语义：GPU 缓存是否需要重传
  int checksum = 200;                     // 不变式：checksum == brightness + contrast + saturation

  void setBrightness(int v) {
    brightness = std::clamp(v, -100, 100);
    checksum = brightness + contrast + saturation;
    cacheDirty = true;
  }
  void setLut(std::string name) { lutName = std::move(name); cacheDirty = true; }
  void render() { cacheDirty = false; }   // 渲染完一帧后，缓存与参数同步
  bool invariantOk() const {
    return brightness >= -100 && brightness <= 100 &&
           checksum == brightness + contrast + saturation;
  }
};

struct RawBackup {                        // 外部历史栈手工抄字段，漏掉了 cacheDirty 和 checksum
  int brightness, contrast, saturation;
  std::string lutName;
};

int main() {
  ColorGrader g;
  std::vector<RawBackup> history;

  g.setBrightness(20); g.setLut("TealOrange"); g.render();
  history.push_back({g.brightness, g.contrast, g.saturation, g.lutName});

  g.setBrightness(-40); g.setLut("Kodak2383"); g.render();

  // 坑 ①：外部按裸字段恢复，忘了标脏 cacheDirty，也没恢复 checksum
  const auto& b = history.back();
  g.brightness = b.brightness; g.contrast = b.contrast;
  g.saturation = b.saturation; g.lutName  = b.lutName;
  std::printf("撤销后：bri=%d lut=%s cacheDirty=%s invariantOk=%s\n",
              g.brightness, g.lutName.c_str(),
              g.cacheDirty ? "true" : "false", g.invariantOk() ? "true" : "false");

  // 坑 ②：字段 public 后，外部业务代码直接写越界值，绕过 clamp 与 checksum
  g.brightness = 250;
  std::printf("误写后：bri=%d invariantOk=%s\n",
              g.brightness, g.invariantOk() ? "true" : "false");
}
```

```text
撤销后：bri=20 lut=TealOrange cacheDirty=false invariantOk=false
误写后：bri=250 invariantOk=false
```

### 1.2 破坏封装引发的三个连锁灾难

运行结果立刻暴露了裸字段备份的三个硬伤：

1. **漏存/漏恢复私有状态（半截状态）**：`RawBackup` 只抄了 4 个显式参数，撤销后 `cacheDirty` 依然是 `false`——预览窗口根本不知道参数变了，画面卡在旧的 `Kodak2383` 上；`checksum` 也停留在 `-40` 时的旧值，不变式直接破裂。
2. **内部不变式失守**：一旦为了撤销把字段改成 `public` 或加了无保护 setter，项目里任何其他模块都能随手写 `g.brightness = 250`，轻松绕过 `std::clamp` 与校验逻辑。
3. **历史栈与内部实现强耦合**：日后 `ColorGrader` 把 `brightness` 改成浮点 `exposure` 或新增一条曲线数组，外部 `History` 和 `RawBackup` 全都要跟着改，封装形同虚设。

<figure class="diagram">
<svg viewBox="0 0 700 180" role="img" aria-label="左边是裸字段备份：History 直接读写 ColorGrader 的私有字段，导致外部也能直接篡改且容易漏存内部状态；右边是备忘录模式：ColorGrader 自己打包不透明 Snapshot 交给 History 保管，History 看不到也不能改内部字段"><g style="font-family:var(--mono)"><text x="165" y="20" text-anchor="middle" style="fill:var(--fg);font-size:12.5px;font-weight:700">裸字段备份（破坏封装）</text><rect x="20" y="38" width="135" height="96" rx="5" style="fill:var(--panel);stroke:var(--danger);stroke-width:1.4"/><text x="87" y="58" text-anchor="middle" style="fill:var(--fg);font-size:11.5px;font-weight:700">ColorGrader</text><text x="32" y="78" style="fill:var(--danger);font-size:10.5px">public: bri, con</text><text x="32" y="94" style="fill:var(--danger);font-size:10.5px">public: lutName</text><text x="32" y="110" style="fill:var(--muted);font-size:10.5px">cacheDirty (漏存)</text><text x="32" y="124" style="fill:var(--muted);font-size:10.5px">checksum (被破坏)</text><rect x="215" y="54" width="110" height="64" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.3"/><text x="270" y="80" text-anchor="middle" style="fill:var(--fg);font-size:11.5px;font-weight:700">History</text><text x="270" y="98" text-anchor="middle" style="fill:var(--danger);font-size:10px">直接读写各字段</text><line x1="155" y1="74" x2="215" y2="74" style="stroke:var(--danger);stroke-width:1.5"/><line x1="215" y1="98" x2="155" y2="98" style="stroke:var(--danger);stroke-width:1.5"/><text x="165" y="160" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">字段全暴露，外部可绕过校验</text><text x="525" y="20" text-anchor="middle" style="fill:var(--fg);font-size:12.5px;font-weight:700">备忘录模式（保持封装）</text><rect x="370" y="38" width="125" height="96" rx="5" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.5"/><text x="432" y="58" text-anchor="middle" style="fill:var(--fg);font-size:11.5px;font-weight:700">ColorGrader</text><text x="382" y="78" style="fill:var(--accent);font-size:10.5px">save() → Snapshot</text><text x="382" y="94" style="fill:var(--accent);font-size:10.5px">restore(Snapshot)</text><text x="382" y="114" style="fill:var(--muted);font-size:10.5px">private: 全部状态</text><rect x="520" y="58" width="76" height="54" rx="5" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.5"/><text x="558" y="80" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">Snapshot</text><text x="558" y="98" text-anchor="middle" style="fill:var(--muted);font-size:10px">🔒 不透明</text><rect x="618" y="54" width="72" height="64" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.3"/><text x="654" y="80" text-anchor="middle" style="fill:var(--fg);font-size:11.5px;font-weight:700">History</text><text x="654" y="98" text-anchor="middle" style="fill:var(--muted);font-size:10px">只管存取</text><line x1="495" y1="85" x2="520" y2="85" style="stroke:var(--accent);stroke-width:1.5"/><line x1="596" y1="85" x2="618" y2="85" style="stroke:var(--accent);stroke-width:1.5"/><text x="525" y="160" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">谁拥有状态，谁负责打包与恢复</text></g></svg>
</figure>

**核心原则**：状态属于 `ColorGrader`，那么**打包状态和恢复状态的代码也必须留在 `ColorGrader` 内部**；外部的 `History` 只管按顺序存和取一个个「上了锁的胶囊」，既看不到里面装了什么，也改不了里面的任何字节。

## 2. 备忘录模式

### 2.1 意图与三角色

> **GoF**：在不破坏封装性的前提下，捕获一个对象的内部状态，并在该对象之外保存这个状态。这样以后就可将该对象恢复到原先保存的状态。

大白话：**游戏存档文件——只有游戏引擎自己能读懂和写回存档里的内部数据，操作系统（文件管理器）只负责把存档放进文件夹、按需要原样交还给引擎。** 三个角色：

- **Originator（发起人，`ColorGrader`）**：拥有真正内部状态的对象；提供 `save()` 创建备忘录、`restore(Snapshot)` 用备忘录恢复自己。
- **Memento（备忘录，`ColorGrader::Snapshot`）**：保存发起人内部状态的「封口胶囊」；对发起人开放全部内部字段（宽接口），对其他对象几乎完全不透明（窄接口，最多暴露 `label()` / 时间戳等只读元数据）。
- **Caretaker（负责人，`History`）**：负责管理备忘录的生命周期与历史栈（`undoStack_` / `redoStack_`），**绝不检查或修改**备忘录里的状态内容。

### 2.2 用 `friend` + 不透明 `Snapshot` 重构调色台

在 C++ 里，最地道的备忘录实现是把 `Snapshot` 写成 `ColorGrader` 的嵌套类，把构造函数和所有状态成员设为 `private`，再声明 `friend class ColorGrader;`：

```cpp
// 《备忘录模式》：经典写法——Snapshot 私有构造 + friend class ColorGrader，History 只能存取不透明快照
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

class ColorGrader {
 public:
  class Snapshot {                        // Memento：窄接口只暴露 label()，宽接口仅对友元开放
   public:
    const std::string& label() const { return label_; }
   private:
    friend class ColorGrader;
    Snapshot(std::string l, int b, int c, int s, std::string lut, int rev, int sum)
        : label_(std::move(l)), brightness_(b), contrast_(c),
          saturation_(s), lutName_(std::move(lut)), revision_(rev), checksum_(sum) {}
    std::string label_;
    int brightness_, contrast_, saturation_;
    std::string lutName_;
    int revision_, checksum_;
  };

  void setBrightness(int v) { brightness_ = std::clamp(v, -100, 100); touch(); }
  void setLut(std::string n){ lutName_ = std::move(n); touch(); }
  void render()             { cacheDirty_ = false; }

  Snapshot save(std::string label) const {
    return Snapshot(std::move(label), brightness_, contrast_, saturation_,
                    lutName_, revision_, checksum_);
  }
  void restore(const Snapshot& s) {
    brightness_ = s.brightness_; contrast_ = s.contrast_; saturation_ = s.saturation_;
    lutName_ = s.lutName_; revision_ = s.revision_; checksum_ = s.checksum_;
    cacheDirty_ = true;                   // 恢复后自动标脏，下一帧必定重绘
  }
  void dump(const char* tag) const {
    std::printf("%-10s → bri=%+3d con=%3d lut=%-10s rev=%d dirty=%s ok=%s\n",
                tag, brightness_, contrast_, lutName_.c_str(), revision_,
                cacheDirty_ ? "true" : "false",
                (checksum_ == brightness_ + contrast_ + saturation_) ? "true" : "false");
  }

 private:
  void touch() { ++revision_; checksum_ = brightness_ + contrast_ + saturation_; cacheDirty_ = true; }
  int brightness_ = 0, contrast_ = 100, saturation_ = 100;
  std::string lutName_ = "None";
  int revision_ = 0, checksum_ = 200;
  bool cacheDirty_ = true;
};

class History {                           // Caretaker：只管双栈，看不到也改不了 Snapshot 内部
 public:
  void backup(ColorGrader::Snapshot s) { undo_.push_back(std::move(s)); redo_.clear(); }
  bool undo(ColorGrader& g) {
    if (undo_.empty()) return false;
    redo_.push_back(g.save("重做点"));
    g.restore(undo_.back()); undo_.pop_back();
    return true;
  }
  bool redo(ColorGrader& g) {
    if (redo_.empty()) return false;
    undo_.push_back(g.save("撤销点"));
    g.restore(redo_.back()); redo_.pop_back();
    return true;
  }
 private:
  std::vector<ColorGrader::Snapshot> undo_, redo_;
};

int main() {
  ColorGrader g; History h; g.render();
  h.backup(g.save("初始"));
  g.setBrightness(20); g.setLut("TealOrange"); g.render(); g.dump("调色 1");
  h.backup(g.save("调色 1"));
  g.setBrightness(-40); g.setLut("Kodak2383"); g.render(); g.dump("调色 2");
  h.undo(g); g.dump("撤销 1 次");
  h.undo(g); g.dump("撤销 2 次");
  h.redo(g); g.dump("重做 1 次");
}
```

```text
调色 1   → bri=+20 con=100 lut=TealOrange rev=2 dirty=false ok=true
调色 2   → bri=-40 con=100 lut=Kodak2383  rev=4 dirty=false ok=true
撤销 1 次 → bri=+20 con=100 lut=TealOrange rev=2 dirty=true ok=true
撤销 2 次 → bri= +0 con=100 lut=None       rev=0 dirty=true ok=true
重做 1 次 → bri=+20 con=100 lut=TealOrange rev=2 dirty=true ok=true
```

注意三个变化：
- 外部代码既不能构造 `Snapshot`（构造函数 `private`），也不能读取或篡改 `s.brightness_`（编译期直接报错）；
- `restore()` 在 `ColorGrader` 内部完成，自动把 `cacheDirty_` 置 `true`、同步恢复 `revision_` 和 `checksum_`，所有不变式恒为 `true`；
- `History` 不依赖任何具体调色字段，未来给 `ColorGrader` 增加色温、色相曲线时，`History` 一行不用改。

### 2.3 UML 类图、时序图与交互演示

```uml
label: 备忘录模式的类图：ColorGrader（Originator）创建并恢复私有状态封口的 Snapshot（Memento），History（Caretaker）仅聚合 Snapshot 而不访问其内部字段
classes:
  - { id: ColorGrader, x: 20, y: 20, w: 230, attrs: ['- brightness_, contrast_, saturation_', '- lutName_, revision_, checksum_', '- cacheDirty_ : bool'], ops: ['+ save(label) : Snapshot', '+ restore(s: const Snapshot&)'] }
  - { id: Snapshot, x: 300, y: 20, w: 215, attrs: ['- label_, brightness_, ...', '- revision_, checksum_'], ops: ['+ label() : const string&', '- Snapshot(...)  <<friend>>'] }
  - { id: History, x: 560, y: 20, w: 185, attrs: ['- undo_ : vector<Snapshot>', '- redo_ : vector<Snapshot>'], ops: ['+ backup(s: Snapshot)', '+ undo(g: ColorGrader&)', '+ redo(g: ColorGrader&)'] }
rels:
  - { from: ColorGrader, to: Snapshot, type: depend, label: '<<create / friend>>' }
  - { from: History, to: Snapshot, type: aggr, label: '0..*' }
```

```uml
label: 时序图：存档时用户先让 ColorGrader 生成 Snapshot 再交给 History；撤销时 History 先把当前状态存进 redo 栈，再把 undo 栈顶交回 ColorGrader::restore()
participants:
  - { id: u, name: '用户', x: 10, w: 60 }
  - { id: g, name: 'ColorGrader', x: 120, w: 115 }
  - { id: s, name: 'Snapshot', x: 295, w: 100 }
  - { id: h, name: 'History', x: 455, w: 100 }
messages:
  - { from: u, to: g, text: 'save("调色 1")', n: 1 }
  - { from: g, to: s, text: '<<create>> 拷贝私有状态', n: 2 }
  - { from: g, to: u, text: 'snap', type: reply }
  - { from: u, to: h, text: 'backup(snap)', n: 3 }
  - { from: u, to: g, text: 'setBrightness(-40)', n: 4 }
  - { from: u, to: h, text: 'undo(grader)', n: 5 }
  - { from: h, to: g, text: 'save("重做点") 压入 redo_', n: 6 }
  - { from: h, to: g, text: 'restore(undo_.back())', n: 7 }
  - { from: g, to: s, text: '友元读取私有字段', n: 8 }
  - { from: h, to: u, text: 'true', type: reply }
```

下面的交互演示展示了存档、撤销/重做、撤销后新编辑清空重做栈、COW 共享 LUT 以及有界历史栈四个过程：

<div class="dsviz" data-viz="dp-memento"></div>

## 3. 深入备忘录：双接口与大状态优化

### 3.1 窄接口与宽接口：C++ 的两种落地方式

GoF 强调备忘录必须提供两套可见性不同的接口：

- **窄接口（Narrow Interface）**：面向 `History` 和外部 UI，只能传递、销毁，最多读取 `label()` / `timestamp()` 等只读展示信息。
- **宽接口（Wide Interface）**：仅面向 `Originator`，允许读写全部内部状态。

C++ 里主要有两种实现手法：

| 手法 | 做法 | 优点 | 缺点 |
| --- | --- | --- | --- |
| **私有嵌套类 + `friend`**（推荐） | `Snapshot` 构造与成员全 `private`，`friend class ColorGrader` | 零虚函数开销、值语义直接放 `std::vector`，编译期强制隔离 | `History` 绑定到具体类型 `ColorGrader::Snapshot` |
| **抽象基类 `IMemento`（窄）+ 派生类（宽）** | `History` 持有 `std::unique_ptr<IMemento>`，`Originator::restore()` 内部 `static_cast` / `dynamic_cast` 回具体子类 | 一个通用 `History` 可管多种不同 `Originator` 的快照 | 多一次堆分配与向下转型，需防把 A 对象的快照喂给 B 对象 |

绝大多数单文档编辑器里，**值语义 + 嵌套类 `friend`** 足够干净；只有通用历史管理器需要统一管理多个异构模块时，才考虑抽象 `IMemento`（或者直接让每个快照闭包成 `std::function<void()>`）。

### 3.2 大状态时的内存爆炸与三条治法

第 2.2 节的 `Snapshot` 只有几个 `int` 和 `std::string`，存 1000 个也不过几十 KB。但真实音视频 / 图像软件里，状态里常常夹带**大块只读或低频变更的数据**——例如调色台里的 3D LUT 查找表（32³ × 2 = 65536 个 `float` = **256 KB**），或者 PS 里的图层像素缓冲（几十 MB）。

如果每次微调亮度（只改了一个 `int`！）都把整张 256 KB 的 LUT 深拷贝进 `Snapshot`，1000 次微调就会吃掉 **250 MB** 内存。工程上常用的三条治法：

1. **共享不可变大对象（写时复制 COW）**：把大对象包在 `std::shared_ptr<const LutTable>` 里。调亮度/对比度时，所有快照共享同一份 `const LutTable`（零拷贝，只增引用计数）；只有真正切换或修改 LUT 时，才分配新表并替换指针。
2. **限制历史栈深度（有界 `std::deque`）**：用 `std::deque<Snapshot>` 代替 `std::vector`，设置上限 `maxDepth`（例如 Photoshop 默认 50 步），压栈满时 `pop_front()` 淘汰最老快照，避免长时间编辑把内存撑爆：

```cpp
// 《备忘录模式》：有界历史栈（std::deque）——超出 maxDepth 时 O(1) 淘汰最老快照，新编辑清空重做栈
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

class ColorGrader {
 public:
  class Snapshot {
   public:
    const std::string& label() const { return label_; }
   private:
    friend class ColorGrader;
    Snapshot(std::string l, int b, int c) : label_(std::move(l)), brightness_(b), contrast_(c) {}
    std::string label_;
    int brightness_, contrast_;
  };
  void set(int b, int c) { brightness_ = b; contrast_ = c; }
  Snapshot save(std::string l) const { return Snapshot(std::move(l), brightness_, contrast_); }
  void restore(const Snapshot& s) { brightness_ = s.brightness_; contrast_ = s.contrast_; }
  void dump(const char* tag) const { std::printf("%-10s → bri=%+3d con=%3d\n", tag, brightness_, contrast_); }
 private:
  int brightness_ = 0, contrast_ = 100;
};

class BoundedHistory {
 public:
  explicit BoundedHistory(size_t maxDepth) : maxDepth_(maxDepth) {}
  void backup(ColorGrader::Snapshot s) {
    if (undo_.size() == maxDepth_) undo_.pop_front(); // 超出上限，O(1) 丢弃最老快照
    undo_.push_back(std::move(s));
    redo_.clear();                                    // 新编辑使旧重做分支作废
  }
  bool undo(ColorGrader& g) {
    if (undo_.empty()) return false;
    redo_.push_back(g.save("redo"));
    g.restore(undo_.back()); undo_.pop_back();
    return true;
  }
  bool redo(ColorGrader& g) {
    if (redo_.empty()) return false;
    if (undo_.size() == maxDepth_) undo_.pop_front();
    undo_.push_back(g.save("undo"));
    g.restore(redo_.back()); redo_.pop_back();
    return true;
  }
  size_t undoSize() const { return undo_.size(); }
  size_t redoSize() const { return redo_.size(); }
 private:
  size_t maxDepth_;
  std::deque<ColorGrader::Snapshot> undo_;
  std::vector<ColorGrader::Snapshot> redo_;
};

int main() {
  ColorGrader g; BoundedHistory h(2);                 // 最多保留 2 步撤销
  h.backup(g.save("v0")); g.set(10, 105);
  h.backup(g.save("v1")); g.set(20, 110);
  h.backup(g.save("v2")); g.set(30, 115);             // v0 被淘汰
  std::printf("压入 3 次（上限 2）：undoSize=%zu\n", h.undoSize());
  h.undo(g); g.dump("撤销 1 次");
  h.undo(g); g.dump("撤销 2 次");
  std::printf("尝试第 3 次撤销：%s\n", h.undo(g) ? "成功" : "已到栈底");
  h.redo(g); g.dump("重做 1 次");
  h.backup(g.save("分支")); g.set(99, 150);
  std::printf("新编辑后 redoSize=%zu\n", h.redoSize());
}
```

```text
压入 3 次（上限 2）：undoSize=2
撤销 1 次 → bri=+20 con=110
撤销 2 次 → bri=+10 con=105
尝试第 3 次撤销：已到栈底
重做 1 次 → bri=+20 con=110
新编辑后 redoSize=0
```

3. **关键帧快照 + 增量命令混合（Checkpoint + Command）**：与视频编码的 **I 帧 + P 帧** 同理——每 20 步存一张全量备忘录，中间 19 步只存轻量的增量命令（Command）。回退到第 37 步时，先瞬间恢复第 20 步的备忘录，再正向重放 17 条增量命令，兼顾内存占用与大跨度跳转速度：

<figure class="diagram">
<svg viewBox="0 0 680 112" role="img" aria-label="关键帧快照与增量命令混合示意图：每隔若干步存一张全量备忘录，中间只存轻量增量命令；回退到第 37 步时先恢复第 20 步的全量快照再重放 17 条命令"><g style="font-family:var(--mono)"><rect x="20" y="24" width="90" height="40" rx="5" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.6"/><text x="65" y="42" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">快照 #0</text><text x="65" y="57" text-anchor="middle" style="fill:var(--accent);font-size:10px">全量 I 帧</text><line x1="110" y1="44" x2="140" y2="44" style="stroke:var(--border-2);stroke-width:1.4"/><rect x="140" y="30" width="58" height="28" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><text x="169" y="48" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">cmd 1..19</text><line x1="198" y1="44" x2="228" y2="44" style="stroke:var(--border-2);stroke-width:1.4"/><rect x="228" y="24" width="90" height="40" rx="5" style="fill:var(--accent-soft);stroke:var(--accent);stroke-width:1.6"/><text x="273" y="42" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">快照 #20</text><text x="273" y="57" text-anchor="middle" style="fill:var(--accent);font-size:10px">全量 I 帧</text><line x1="318" y1="44" x2="348" y2="44" style="stroke:var(--link);stroke-width:1.5"/><rect x="348" y="30" width="96" height="28" rx="4" style="fill:var(--panel);stroke:var(--link);stroke-width:1.4"/><text x="396" y="48" text-anchor="middle" style="fill:var(--link);font-size:10.5px">重放 cmd 21..37</text><line x1="444" y1="44" x2="474" y2="44" style="stroke:var(--link);stroke-width:1.5"/><rect x="474" y="24" width="90" height="40" rx="5" style="fill:var(--panel);stroke:var(--warn);stroke-width:1.5"/><text x="519" y="42" text-anchor="middle" style="fill:var(--fg);font-size:11px;font-weight:700">目标 #37</text><text x="519" y="57" text-anchor="middle" style="fill:var(--warn);font-size:10px">恢复完成</text><text x="20" y="96" style="fill:var(--muted);font-size:10.5px">每 20 步存 1 份备忘录，中间存轻量命令：内存省 90%+，大跨度撤销最多重放 19 步</text></g></svg>
</figure>

### 3.3 工程落地时的三个注意点

1. **快照里绝不存裸指针或外部资源句柄**：如果 `Snapshot` 里存了指向外部临时缓存的裸指针，或存了已释放的 OpenGL/Metal 纹理 ID，撤销回几分钟前的快照时就会直接踩到悬空指针。快照里的资源要么**按值自包含**，要么用 `std::shared_ptr<const T>` 延长只读生命周期，要么存可重新加载的不可变资源键（如 LUT 文件哈希）。
2. **连续拖动只存「按下前」一份快照**：用户拖动亮度滑块从 `0` 拖到 `+60` 会触发上百次 `onDrag` 事件。不要在每次 `onDrag` 里都调 `backup(g.save())`，而是在 `onDragStart` 存一份起点快照、拖动中实时预览、松手时若值有变化才正式提交，否则一次拖动就会把整个 50 步撤销栈全部冲刷掉。
3. **真实工程里的备忘录**：DaVinci Resolve / Lightroom 的调色快照与历史面板、数据库事务里的 **`SAVEPOINT` / `ROLLBACK TO`**、游戏引擎的快速存档（Quick Save），本质都是备忘录模式。

## 4. 现代 C++ 写法与实测

### 4.1 值语义 + `std::shared_ptr<const T>` 共享大对象备忘录

关键点在于 **`const`**：一定要写 `std::shared_ptr<const LutTable>` 而不是 `std::shared_ptr<LutTable>`。只要类型带 `const`，编译器就会帮我们兜底——谁也无法通过某个指针原地修改 LUT 内容，从而彻底杜绝「改当前状态却悄悄污染历史快照」的隐蔽 bug：

```cpp
// 《备忘录模式》：现代 C++——用 std::shared_ptr<const LutTable> 共享不可变大对象（COW 备忘录）
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct LutTable {
  std::string name;
  std::vector<float> data;                // 65536 个 float = 256 KB
  explicit LutTable(std::string n) : name(std::move(n)), data(65536, 1.0f) {}
};

class ColorGrader {
 public:
  class Snapshot {
   public:
    const std::string& label() const { return label_; }
    long lutRefCount() const { return lut_.use_count(); }
   private:
    friend class ColorGrader;
    Snapshot(std::string label, int b, int c, int s, std::shared_ptr<const LutTable> lut)
        : label_(std::move(label)), brightness_(b), contrast_(c),
          saturation_(s), lut_(std::move(lut)) {}
    std::string label_;
    int brightness_, contrast_, saturation_;
    std::shared_ptr<const LutTable> lut_; // 指向 const，多个快照安全共享同一块 256 KB 内存
  };

  ColorGrader() : lut_(std::make_shared<const LutTable>("None")) {}
  void setBrightness(int v) { brightness_ = std::clamp(v, -100, 100); }
  void setContrast(int v)   { contrast_   = std::clamp(v, 0, 200); }
  void setLut(std::shared_ptr<const LutTable> lut) { lut_ = std::move(lut); }

  Snapshot save(std::string label) const {
    return Snapshot(std::move(label), brightness_, contrast_, saturation_, lut_);
  }
  void restore(const Snapshot& s) {
    brightness_ = s.brightness_; contrast_ = s.contrast_;
    saturation_ = s.saturation_; lut_ = s.lut_;
  }
  void dump(const char* tag) const {
    std::printf("%-10s → bri=%+3d con=%3d lut=%-10s (use_count=%ld)\n",
                tag, brightness_, contrast_, lut_->name.c_str(), lut_.use_count());
  }

 private:
  int brightness_ = 0, contrast_ = 100, saturation_ = 100;
  std::shared_ptr<const LutTable> lut_;
};

int main() {
  ColorGrader g; std::vector<ColorGrader::Snapshot> history;
  auto teal = std::make_shared<const LutTable>("TealOrange");
  g.setLut(teal);
  history.push_back(g.save("加载 TealOrange"));

  g.setBrightness(10); history.push_back(g.save("亮度 +10"));
  g.setContrast(120);  history.push_back(g.save("对比度 120"));
  g.dump("微调后");

  g.setLut(std::make_shared<const LutTable>("Kodak2383"));
  g.setBrightness(-15);
  g.dump("切换 LUT");

  g.restore(history.back());
  g.dump("恢复快照");
  std::printf("history[0] 与 history[2] 共享同一份 LUT，引用计数=%ld\n",
              history[0].lutRefCount());
}
```

```text
微调后  → bri=+10 con=120 lut=TealOrange (use_count=5)
切换 LUT → bri=-15 con=120 lut=Kodak2383  (use_count=1)
恢复快照 → bri=+10 con=120 lut=TealOrange (use_count=5)
history[0] 与 history[2] 共享同一份 LUT，引用计数=5
```

`use_count = 5` 正好对应：`teal` 局部变量 + `g.lut_` + `history[0..2]` 三个快照——5 处持有，堆上只有 **1 份** 256 KB LUT。

### 4.2 实测对比：整份深拷贝 vs COW 共享大对象

用 `projects/dp-lab/27-memento/bench.cpp` 实测 **1000 次调色存档**（每 100 次切换一次 256 KB LUT，共 10 份 LUT，其余 90% 为微调亮度；Apple clang 21，`-O2`，7 轮中位数）：

<figure class="diagram">
<svg viewBox="0 0 680 148" role="img" aria-label="1000 次存档对比：整份深拷贝单次 save 耗时 2538.8 ns、总内存 250.06 MB；COW 共享单次 save 耗时 3.6 ns、总内存 2.53 MB"><g style="font-family:var(--mono)"><text x="115" y="18" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">单次 save() 耗时（Clang -O2）</text><rect x="115" y="30" width="210" height="20" rx="2" style="fill:var(--danger)"/><text x="331" y="45" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">2538.8 ns</text><rect x="115" y="60" width="2" height="20" rx="2" style="fill:var(--accent)"/><text x="123" y="75" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">3.6 ns（快 708.6×）</text><text x="430" y="18" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">1000 份快照总内存</text><rect x="430" y="30" width="165" height="20" rx="2" style="fill:var(--danger)"/><text x="601" y="45" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">250.06 MB</text><rect x="430" y="60" width="3" height="20" rx="2" style="fill:var(--accent)"/><text x="439" y="75" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">2.53 MB（降至 1.0%）</text><text x="107" y="45" text-anchor="end" style="fill:var(--fg);font-size:11.5px">整份深拷贝</text><text x="107" y="75" text-anchor="end" style="fill:var(--fg);font-size:11.5px">COW 共享</text><text x="10" y="116" text-anchor="start" style="fill:var(--muted);font-size:10.5px">条件：1000 次存档，每 100 次切一次 256 KB LUT（共 10 份），其余 90% 仅微调标量参数</text><text x="10" y="134" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench.cpp：Apple clang 21，-O2，7 轮中位数；完整脚本见 projects/dp-lab/27-memento/</text></g></svg>
</figure>

| 方案 | 单次 `save()` 耗时 | 1000 份快照总内存 | `sizeof(Snapshot)` | 适用场景 |
| --- | :---: | :---: | :---: | --- |
| **整份深拷贝** | 2538.8 ns（2.54 µs） | 250.06 MB | 64 B | 状态全是极小标量（几十字节内），无需引入智能指针 |
| **COW 共享 (`shared_ptr<const>`)** | **3.6 ns** | **2.53 MB** | **32 B** | **现代 C++ 首选**：含大缓冲区 / LUT / 图层树，高频微调参数 |

单次 `save()` 提速 **708.6 倍**，内存直降 **99%**——核心原因只是把「高频变动的小参数」按值拷贝、把「低频变动的大块只读数据」通过 `shared_ptr<const T>` 共享。

## 5. 备忘录与相似的模式

| 模式 | 关注点 | 撤销方式与核心区别 |
| --- | --- | --- |
| **命令（Command）** | 封装「**动作 / 增量**」 | 记录每一步做了什么及逆操作（如 `Move +10px` / `Move -10px`）；省内存，但复杂非可逆操作（如高斯模糊、自动白平衡）很难写逆操作 |
| **备忘录（Memento）** | 封装「**状态快照**」 | 直接拍下某刻内部状态的封口快照；不关心操作是否可逆，恢复绝对精确，但大状态需配合 COW 或栈深限制 |
| **原型（Prototype）** | 通过 `clone()` **创建新对象** | 目标是复制出一个**独立可编辑的新实例**继续干活；备忘录是为了日后**恢复原对象**，且对外隐藏内部结构 |
| **序列化（Serialization）** | 把状态转成**跨进程/落盘字节流** | 关注持久化与跨版本兼容（JSON / Protobuf）；备忘录通常是内存里的轻量对象，不要求对外可读，也不必经过编解码开销 |

工程里最常见的组合是**命令 + 备忘录**：可逆小操作走命令，不可逆重操作（如「一键自动调色」「应用滤镜」）在 `Command::execute()` 前向接收者要一份 `Snapshot` 塞进命令对象里，撤销时直接 `restore()`。

## 6. 小结

- 备忘录模式的核心不是「存个备份」，而是**在不破坏封装的前提下存档与恢复**：谁拥有状态，谁负责打包和解包，避免外部漏存私有字段（如 `cacheDirty_`）或绕过校验破坏不变式。
- C++ 里最干净的窄/宽接口实现是**私有嵌套类 `Snapshot` + `friend class Originator`**，零虚函数开销、天然值语义。
- 面对大状态备忘录，用 **`std::shared_ptr<const T>` 实现 COW 共享**（实测 1000 次存档提速 **708.6×**，内存从 **250.06 MB** 降到 **2.53 MB**），再叠加有界 `std::deque` 控制栈深，或与增量命令做关键帧混合。
- 可逆增量用命令，不可逆操作或强封装状态恢复用备忘录，二者常常搭配使用。

## 7. 练习

1. **辨析**：下面三个场景，分别更适合用备忘录、命令、还是「备忘录 + 命令」混合？
   - (a) 文本编辑器里用户输入/删除单个字符，需要支持 10 万步撤销；
   - (b) 游戏里的「快速存档 / 读档（F5 / F9）」，需要完整还原角色坐标、背包、AI 状态机、任务触发器等上百个私有字段；
   - (c) 图像软件既有「移动图层 (+10, -5)」，又有「一键高斯模糊（不可逆）」。
2. **设计/改写**：回头看第 2.2 节的 `History::undo(ColorGrader& g)`：为什么必须先执行 `redo_.push_back(g.save("重做点"))`，再把 `undo_.back()` 恢复给 `g` 并弹出，而**不能**直接把 `undo_.back()` 挪进 `redo_`？如果要支持「拖动滑块时只在按下前存 1 份快照，松手后未改动则丢弃」，该怎么给 `History` 加接口？
3. **找坑**：下面的色调曲线编辑器 `CurveEditor` 想用 `std::shared_ptr` 做共享备忘录，但运行后「恢复到初始快照 `s0`」打印的却是 `180` 而不是 `128`。找出根本原因并说明怎么改。

<!-- run: off -->
```cpp
// 《备忘录模式》练习 3（有 bug）：共享可变对象导致历史快照被悄悄污染
struct Curve { std::vector<int> pts; };

class CurveEditor {
 public:
  class Snapshot {
    friend class CurveEditor;
    std::shared_ptr<Curve> curve_;                  // 坑 ①：缺少 const
    explicit Snapshot(std::shared_ptr<Curve> c) : curve_(std::move(c)) {}
  };
  CurveEditor() : curve_(std::make_shared<Curve>(Curve{{0, 128, 255}})) {}
  void setMidpoint(int v) { curve_->pts[1] = v; }   // 坑 ②：直接原地修改共享对象！
  Snapshot save() const { return Snapshot(curve_); }
  void restore(const Snapshot& s) { curve_ = s.curve_; }
  int midpoint() const { return curve_->pts[1]; }
 private:
  std::shared_ptr<Curve> curve_;
};

int main() {
  CurveEditor ed;
  auto s0 = ed.save();       // s0.curve_ 与 ed.curve_ 指向同一个 Curve
  ed.setMidpoint(180);       // 把 s0 里的 pts[1] 也改成了 180！
  ed.restore(s0);
  std::printf("恢复后 midpoint=%d\n", ed.midpoint()); // 错误输出 180
}
```

<details>
<summary>点击查看答案</summary>

1. (a) **命令**：单字符增删可逆且极轻量，每步存全量文本快照会瞬间爆内存；
   (b) **备忘录**：上百个内部私有状态无法用逆操作还原，且存档管理器不应窥探游戏实体内部；
   (c) **备忘录 + 命令混合**：移动图层走轻量命令，高斯模糊命令在 `execute()` 前向图层要一份像素备忘录，撤销时直接 `restore()`。

2. 因为 `undo_.back()` 里存的是**修改前的旧状态**，而 `g` 此刻持有的是**修改后的最新状态**；如果直接把 `undo_.back()` 挪进 `redo_`，重做时就会恢复成「旧状态」而不是被撤销掉的「新状态」。对于连续拖动，可以增加 `beginTransaction(g.save("拖动"))` 暂存起点快照，松手时若当前参数与起点不同才调 `commit()` 压入 `undo_` 并清空 `redo_`；完整有界 Undo/Redo 见 `projects/dp-lab/27-memento/ex2_redo.cpp`。

3. **浅拷贝共享了可变对象**：`s0` 和 `ed` 持有指向同一块堆内存的 `std::shared_ptr<Curve>`，`setMidpoint(180)` 原地修改 `curve_->pts[1]`，把历史快照 `s0` 也一并改坏了。
   **修复**：① 把指针类型改成 `std::shared_ptr<const Curve>`，让编译器禁止原地修改；② 在 `setMidpoint()` 里执行写时复制（`auto next = std::make_shared<Curve>(*curve_); next->pts[1] = v; curve_ = std::move(next);`），旧快照自然不受影响。完整修复版见 `projects/dp-lab/27-memento/ex3_limit.cpp`。

</details>
