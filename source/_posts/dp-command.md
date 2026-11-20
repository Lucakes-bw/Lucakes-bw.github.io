---
title: 命令模式
date: 2026-11-11 22:00:00
updated: 2026-11-11 22:00:00
categories:
  - 设计模式
  - 行为型模式
tags:
  - 设计模式
  - 命令
  - 撤销重做
  - 宏命令
  - 任务队列
  - 操作日志
  - std::function
  - variant
order: 3
description: 给一个简单的视频剪辑器加撤销：直接改状态的写法不知道「改之前是什么样」，只能每次操作前把整条时间线复制一份，200 个片段做 1000 次操作，撤销历史占了 34368 KB（Clang）/ 37345 KB（GCC），每次操作 4.58 µs。命令模式把每个操作封装成带 execute / undo 的对象，只记「改了什么、原来是什么」：同样 1000 次操作只占 47 KB，撤销 1000 步 32 µs（快照版 2902 µs）。在此基础上讲宏命令（波纹删除）、命令合并（一次拖动的 120 条移动合成 1 条）、命令队列和后台转码、把命令写成操作日志做崩溃恢复（1000 条命令的日志 22582 字节）；现代 C++ 里用一对 lambda、std::variant 写命令，variant 每条只要 1.51 ns、零次堆分配，以及按引用捕获导致的 heap-use-after-free；最后分清命令和策略、备忘录、责任链。
top_group_list: true
disableNunjucks: true
viz: [dp-command]
---

> **本文目标**：理解命令模式——把「要做的一件事」封装成对象，让调用者只依赖 `Command` 接口；掌握带 `execute / undo` 的撤销/重做栈、宏命令与命令合并；了解命令队列与操作日志（崩溃恢复）的思路；会用 `std::variant` / lambda 写现代 C++ 命令并避开悬空捕获；通过实测对比快照与命令的开销；分清命令、策略、备忘录与责任链。
>
> **前置知识**：上一篇《观察者模式》；《策略模式》；《组合模式》（宏命令即命令的组合）；lambda 与 `std::function`。
>
> **编译环境**：`g++ / clang++ -std=c++17`，完整快照压测、后台队列、操作日志与 ASan 复现代码见 `projects/dp-lab/20-command/`。

## 1. 问题：给剪辑器加上撤销

### 1.1 直接改状态与快照痛点

做一个视频剪辑器，时间线上排着剪自 `bbb-10s.mp4` 的片段，支持移动、调音量、加滤镜、剪切。最直接的写法是每个方法直接修改时间线：

<!-- run: off -->
```cpp
class Editor {
public:
    void move(int id, int start)          { save(); tl_.find(id)->start = start; }
    void setVolume(int id, double v)      { save(); tl_.find(id)->volume = v; }
    void addFilter(int id, std::string f) { save(); tl_.find(id)->filters.push_back(std::move(f)); }
    bool undo() {
        if (history_.empty()) return false;
        tl_ = std::move(history_.back());
        history_.pop_back();
        return true;
    }
private:
    void save() { history_.push_back(tl_); }      // 每次修改前复制整条时间线
    Timeline& tl_;
    std::vector<Timeline> history_;
};
```

直接改状态的写法在调用结束时就丢掉了旧值；想支持 `Ctrl+Z`，只能每次操作前把**整条时间线**复制一份存进 `history_`。用 `projects/dp-lab/20-command/direct_edit.cpp` 实测 200 个片段的时间线：

- 时间线本身仅占 **30.9 KB**；
- 每次只改一个片段的一个字段，却复制了全部 200 个片段：操作 1000 次后，撤销历史多占 **26586.5 KB**（Clang）/ **29321 KB**（GCC），且随片段数线性暴涨。

### 1.2 根本问题：操作没有变成对象

内存暴涨只是表象，更根本的问题是「一次函数调用」调完就没了：
- 菜单上显示不出「撤销 移动 #3」（快照里只有状态，没有操作语义）；
- 拖动片段产生上百次 mousemove，没法把连续移动合并成一步；
- 「波纹删除 = 删片段 + 后面整体左移」这类组合操作没法一次撤销；
- 无法把耗时操作（后台转码）排进队列，也无法把操作记成日志做崩溃恢复。

## 2. 命令模式

### 2.1 意图与角色

> **GoF**：把一个请求封装成一个对象，从而可以用不同的请求把客户参数化、把请求排队或者记录日志，以及支持可撤销的操作。

大白话：**把「要做的一件事」变成一个对象**，它带着参数、能 `execute()`、还能 `undo()`。有了对象，就能进栈、合并、排队、序列化。

五个角色：
- **Command（命令接口）**：统一的 `execute / undo` 接口；
- **ConcreteCommand（具体命令）**：具体操作（`MoveClip`、`SetVolume`、`SplitClip` 等），自己保存参数和撤销所需的旧值；
- **Receiver（接收者）**：真正被修改的对象（`Timeline`）；
- **Invoker（调用者）**：只认识 `Command` 接口、管理撤销栈与重做栈（`History`）；
- **Client（客户）**：界面或快捷键，创建具体命令并交给 `History`。

### 2.2 用命令模式重构

每个操作写成一个 `Command` 子类，`execute()` 执行并记录旧值，`undo()` 用旧值恢复；`History` 维护撤销栈 `done_` 和重做栈 `undone_`：

```cpp
// 《命令模式》：把剪辑操作封装成 Command，History 维护撤销栈和重做栈
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Clip {
    int id, start, in, out;
    double volume = 1.0;
    std::vector<std::string> filters;
    int end() const { return start + out - in; }
};

struct Timeline {
    std::vector<Clip> clips;
    int indexOf(int id) const {
        for (int i = 0; i < (int)clips.size(); ++i) if (clips[i].id == id) return i;
        return -1;
    }
    Clip* find(int id) { int i = indexOf(id); return i < 0 ? nullptr : &clips[i]; }
};

struct Command {
    virtual ~Command() = default;
    virtual void execute(Timeline& tl) = 0;
    virtual void undo(Timeline& tl) = 0;
    virtual std::string name() const = 0;
};

struct MoveClip : Command {
    int id, to, from = 0;
    MoveClip(int i, int t) : id(i), to(t) {}
    void execute(Timeline& tl) override { auto* c = tl.find(id); from = c->start; c->start = to; }
    void undo(Timeline& tl) override { tl.find(id)->start = from; }
    std::string name() const override { return "移动 #" + std::to_string(id); }
};

struct SetVolume : Command {
    int id; double v, old = 0;
    SetVolume(int i, double x) : id(i), v(x) {}
    void execute(Timeline& tl) override { auto* c = tl.find(id); old = c->volume; c->volume = v; }
    void undo(Timeline& tl) override { tl.find(id)->volume = old; }
    std::string name() const override { return "调音量 #" + std::to_string(id); }
};

struct SplitClip : Command {                      // newId 在构造时定好，保证重做时 id 不变
    int id, at, newId;
    SplitClip(int i, int a, int nid) : id(i), at(a), newId(nid) {}
    void execute(Timeline& tl) override {
        int i = tl.indexOf(id);
        Clip right = tl.clips[i];
        int cut = right.in + (at - right.start);
        right.id = newId; right.start = at; right.in = cut;
        tl.clips[i].out = cut;
        tl.clips.insert(tl.clips.begin() + i + 1, right);
    }
    void undo(Timeline& tl) override {
        int r = tl.indexOf(newId);
        tl.find(id)->out = tl.clips[r].out;
        tl.clips.erase(tl.clips.begin() + r);
    }
    std::string name() const override { return "剪切 #" + std::to_string(id); }
};

class History {                                   // 调用者（Invoker）
public:
    explicit History(Timeline& tl) : tl_(tl) {}
    void run(std::unique_ptr<Command> c) {
        c->execute(tl_);
        done_.push_back(std::move(c));
        undone_.clear();                          // 做了新操作，重做栈作废
    }
    bool undo() {
        if (done_.empty()) return false;
        done_.back()->undo(tl_);
        undone_.push_back(std::move(done_.back()));
        done_.pop_back();
        return true;
    }
    bool redo() {
        if (undone_.empty()) return false;
        undone_.back()->execute(tl_);
        done_.push_back(std::move(undone_.back()));
        undone_.pop_back();
        return true;
    }
    void print() const {
        std::printf("  撤销栈："); for (auto& c : done_) std::printf("[%s] ", c->name().c_str());
        std::printf("\n  重做栈："); for (auto& c : undone_) std::printf("[%s] ", c->name().c_str());
        std::printf("\n");
    }
private:
    Timeline& tl_;
    std::vector<std::unique_ptr<Command>> done_, undone_;
};

int main() {
    Timeline tl{{{1, 0, 0, 3000, 1.0, {}}, {2, 3000, 3000, 8000, 1.0, {}}, {3, 8000, 8000, 10000, 1.0, {}}}};
    History h(tl);
    h.run(std::make_unique<MoveClip>(3, 9000));
    h.run(std::make_unique<SetVolume>(2, 0.5));
    h.run(std::make_unique<SplitClip>(2, 5000, 4));
    std::printf("执行 3 条后片段数：%zu\n", tl.clips.size());
    h.print();

    h.undo(); h.undo();
    std::printf("撤销 2 次后：#2 音量 %.1f，片段数 %zu\n", tl.find(2)->volume, tl.clips.size());
    h.redo();
    h.run(std::make_unique<SetVolume>(1, 0.8));
    std::printf("重做 1 次再做新命令，redo()=%s\n", h.redo() ? "true" : "false");
    h.print();
}
```

```text
执行 3 条后片段数：4
  撤销栈：[移动 #3] [调音量 #2] [剪切 #2] 
  重做栈：
撤销 2 次后：#2 音量 1.0，片段数 3
重做 1 次再做新命令，redo()=false
  撤销栈：[移动 #3] [调音量 #2] [调音量 #1] 
  重做栈：
```

写可撤销命令的四条铁律：
1. **旧值在 `execute()` 里记，不在构造函数里记**：重做时会再次调 `execute()`，必须重新记录当时状态；
2. **命令里存 id，不存 `Clip*`**：`vector` 插入/删除会导致指针失效；
3. **新对象 id 在构造时定好**（如 `SplitClip(2, 5000, 4)`）：否则撤销再重做拿到新 id，后续引用 `#4` 的命令会找不到目标；
4. **撤销后做新命令必须清空重做栈**：旧重做分支基于已过期的状态。

### 2.3 类图、时序图与交互演示

```uml
label: 命令模式：History（调用者）聚合任意多个 Command；MoveClip、SetVolume、SplitClip、MacroCommand 实现 Command；具体命令执行时操作 Timeline（接收者）；MacroCommand 又聚合多个 Command
classes:
  - { id: Client, name: '时间线界面 / 快捷键', x: 20, y: 20, w: 170, ops: ['+ onDrag()', '+ onCtrlZ()'] }
  - { id: History, x: 260, y: 20, w: 230, attrs: ['- done_ : vector<unique_ptr<Command>>', '- undone_ : vector<...>'], ops: ['+ run(c)', '+ undo()', '+ redo()'] }
  - { id: Command, stereo: interface, x: 600, y: 20, w: 200, ops: ['*+ execute(tl)', '*+ undo(tl)', '+ mergeWith(next) : bool'] }
  - { id: MoveClip, x: 300, y: 230, w: 140, attrs: ['- id_, to_, from_'], ops: ['+ execute(tl)', '+ undo(tl)'] }
  - { id: SetVolume, x: 460, y: 230, w: 140, attrs: ['- id_, v_, old_'], ops: ['+ execute(tl)', '+ undo(tl)'] }
  - { id: SplitClip, x: 625, y: 230, w: 140, attrs: ['- id_, at_, newId_'], ops: ['+ execute(tl)', '+ undo(tl)'] }
  - { id: MacroCommand, x: 790, y: 230, w: 170, attrs: ['- cmds_ : vector<...>'], ops: ['+ execute(tl)', '+ undo(tl)'] }
  - { id: Timeline, x: 20, y: 230, w: 170, attrs: ['+ clips : vector<Clip>'], ops: ['+ find(id) : Clip*'] }
rels:
  - { from: Client, to: History, type: assoc, points: [[190, 50], [260, 50]] }
  - { from: History, to: Command, type: aggregate, tm: '*', points: [[547, 50], [600, 50]] }
  - { from: MoveClip, to: Command, type: realize, points: [[367, 230], [367, 190], [700, 190], [700, 123]] }
  - { from: SetVolume, to: Command, type: realize, points: [[529, 230], [529, 190], [700, 190], [700, 123]] }
  - { from: SplitClip, to: Command, type: realize, points: [[700, 230], [700, 123]] }
  - { from: MacroCommand, to: Command, type: realize, points: [[871, 230], [871, 190], [700, 190], [700, 123]] }
  - { from: MoveClip, to: Timeline, type: depend, label: '改', points: [[300, 270], [199, 270]] }
  - { from: MacroCommand, to: Command, type: aggregate, tm: '*', points: [[940, 230], [940, 70], [800, 70]] }
```

```uml
label: 时序图：用户按 Ctrl+Z，界面调用 History 的 undo；History 取出撤销栈顶的 SplitClip，调用它的 undo；SplitClip 在 Timeline 上找到两段，删掉后半段、恢复前半段的长度；返回后 History 把命令移到重做栈
participants:
  - { id: u, name: '用户', x: 10, w: 80 }
  - { id: c, name: '时间线界面', x: 140, w: 110 }
  - { id: h, name: 'History', x: 320, w: 100 }
  - { id: s, name: 'SplitClip', x: 490, w: 110 }
  - { id: t, name: 'Timeline', x: 720, w: 100 }
messages:
  - { from: u, to: c, text: 'Ctrl+Z', n: 1, type: async }
  - { from: c, to: h, text: 'undo()' }
  - { from: h, to: s, text: 'undo(tl)', n: 2, note: '撤销栈的栈顶' }
  - { from: s, to: t, text: 'indexOf(4)、find(2)' }
  - { from: t, to: s, text: '', type: reply }
  - { from: s, to: t, text: 'erase(#4)；#2.out = 8000' }
  - { from: t, to: s, text: '', type: reply }
  - { from: s, to: h, text: '', type: reply }
  - { from: h, to: h, text: '移到重做栈', n: 3 }
  - { from: h, to: c, text: '', type: reply }
```

下面的交互演示把撤销/重做栈、命令合并和宏命令倒着撤销合在一起，点按钮看每一步的变化：

<div class="dsviz" data-viz="dp-command"></div>

### 2.4 三种撤销方式实测

除了整份快照和命令，还有折中方案**共享快照**：片段做成不可变 `shared_ptr<const Clip>`，改哪个片段只复制那一个，其余片段新旧快照共用。`projects/dp-lab/20-command/bench_undo.cpp` 用 200 个片段做 1000 次操作（`-O2`，7 轮中位数）：

<figure class="diagram">
<svg viewBox="0 0 740 156" role="img" aria-label="200 个片段的时间线做 1000 次操作后，撤销历史占的内存。Clang：整份快照 34368 KB，共享快照 3344 KB，命令 47 KB。GCC：37345、3353、48 KB"><g style="font-family:var(--mono)"><text x="198.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang / macOS</text><rect x="198.0" y="30.0" width="162.8" height="20.0" rx="2" style="fill:var(--danger)"/><text x="366.8" y="45.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">34368 KB</text><rect x="198.0" y="60.0" width="15.8" height="20.0" rx="2" style="fill:var(--warn)"/><text x="219.8" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">3344 KB</text><rect x="198.0" y="90.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="205.5" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">47 KB</text><text x="468.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC 13 / Ubuntu</text><rect x="468.0" y="30.0" width="176.9" height="20.0" rx="2" style="fill:var(--danger)"/><text x="650.9" y="45.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">37345 KB</text><rect x="468.0" y="60.0" width="15.9" height="20.0" rx="2" style="fill:var(--warn)"/><text x="489.9" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">3353 KB</text><rect x="468.0" y="90.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="475.5" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">48 KB</text><text x="190.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">整份快照</text><text x="190.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">共享快照（改哪个复制哪个）</text><text x="190.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">命令</text><text x="10.0" y="148.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench_undo：-O2，1000 次操作后把历史销毁，统计释放的堆内存；7 轮中位数</text></g></svg>
</figure>

| 指标（200 片段，1000 次操作） | 整份快照 | 共享快照 | 命令 |
| --- | ---: | ---: | ---: |
| 1000 步撤销历史（Clang / GCC） | 34368 / 37345 KB | 3344 / 3353 KB | **47 / 48 KB** |
| 1000 次操作的堆分配次数 | 525911 | 5011 | **1661** |
| 每次操作耗时（Clang / GCC） | 4.58 / 4.79 µs | 0.233 / 0.174 µs | **0.054 / 0.033 µs** |
| 撤销 1000 步（Clang / GCC） | 2902 / 2422 µs | 141 / 148 µs | **32 / 13 µs** |

命令比整份快照省了 **700 多倍内存**，且开销与时间线大小无关。共享快照的优势在于**不用为每种操作写 `undo()`**（函数式状态库常用），而命令模式的代价是每种操作都要自己写对 `undo()`。

## 3. 深入：宏命令、命令合并、队列与日志

### 3.1 宏命令与命令合并

- **宏命令**（组合模式）：把一组命令包成一个 `MacroCommand`（如「波纹删除 = 删片段 + 后续左移」），执行按顺序，**撤销必须倒着来**——否则先撤剪切删掉了新片段，再撤移动就找不到它；中途失败时像数据库事务一样倒着回滚已执行的部分。
- **命令合并**：一次拖动产生上百条 `MoveClip`，给 `Command` 加 `mergeWith(const Command& next)`，同一次拖动（同一 `gesture` 编号、同一片段）只更新终点 `to_`、保留最初的 `from_`。Qt 的 `QUndoCommand::mergeWith()` 与 `beginMacro/endMacro` 就是这套设计。

```cpp
// 《命令模式》：宏命令（波纹删除，倒序撤销）+ 命令合并（120 次拖动合并为 1 条）
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Clip { int id, start, len; };
struct Timeline {
    std::vector<Clip> clips;
    int indexOf(int id) const {
        for (int i = 0; i < (int)clips.size(); ++i) if (clips[i].id == id) return i;
        return -1;
    }
    Clip* find(int id) { int i = indexOf(id); return i < 0 ? nullptr : &clips[i]; }
};

struct Command {
    virtual ~Command() = default;
    virtual void execute(Timeline&) = 0;
    virtual void undo(Timeline&) = 0;
    virtual bool mergeWith(const Command&) { return false; }
};

struct MoveClip : Command {
    int id, to, gesture, from = 0;
    MoveClip(int i, int t, int g = 0) : id(i), to(t), gesture(g) {}
    void execute(Timeline& tl) override { auto* c = tl.find(id); from = c->start; c->start = to; }
    void undo(Timeline& tl) override { tl.find(id)->start = from; }
    bool mergeWith(const Command& next) override {
        auto* m = dynamic_cast<const MoveClip*>(&next);
        if (!m || gesture == 0 || m->gesture != gesture || m->id != id) return false;
        to = m->to;                               // 保留最初起点 from，只更新终点
        return true;
    }
};

struct DeleteClip : Command {
    int id, idx = 0; Clip saved{};
    explicit DeleteClip(int i) : id(i) {}
    void execute(Timeline& tl) override { idx = tl.indexOf(id); saved = tl.clips[idx]; tl.clips.erase(tl.clips.begin() + idx); }
    void undo(Timeline& tl) override { tl.clips.insert(tl.clips.begin() + idx, saved); }
};

struct MacroCommand : Command {                   // 宏命令：执行正序，撤销倒序
    std::vector<std::unique_ptr<Command>> cmds;
    void add(std::unique_ptr<Command> c) { cmds.push_back(std::move(c)); }
    void execute(Timeline& tl) override { for (auto& c : cmds) c->execute(tl); }
    void undo(Timeline& tl) override { for (auto it = cmds.rbegin(); it != cmds.rend(); ++it) (*it)->undo(tl); }
};

class History {
public:
    explicit History(Timeline& tl) : tl_(tl) {}
    void run(std::unique_ptr<Command> c) {
        c->execute(tl_);
        if (!done_.empty() && done_.back()->mergeWith(*c)) return;
        done_.push_back(std::move(c));
    }
    void undo() { if (!done_.empty()) { done_.back()->undo(tl_); done_.pop_back(); } }
    std::size_t size() const { return done_.size(); }
private:
    Timeline& tl_;
    std::vector<std::unique_ptr<Command>> done_;
};

int main() {
    Timeline tl{{{1, 0, 3000}, {2, 3000, 5000}, {3, 8000, 2000}}};
    History h(tl);
    for (int x = 8010; x <= 9200; x += 10) h.run(std::make_unique<MoveClip>(3, x, /*gesture=*/1));
    std::printf("120 次拖动合并后：栈深 %zu，#3 位于 %dms\n", h.size(), tl.find(3)->start);
    h.undo();
    std::printf("按 1 次 Ctrl+Z  ：#3 回到 %dms\n", tl.find(3)->start);

    auto ripple = std::make_unique<MacroCommand>();   // 波纹删除 #2：删 #2 + #3 左移 5000ms
    ripple->add(std::make_unique<DeleteClip>(2));
    ripple->add(std::make_unique<MoveClip>(3, 3000));
    h.run(std::move(ripple));
    std::printf("波纹删除 #2 后  ：片段数 %zu，#3 位于 %dms\n", tl.clips.size(), tl.find(3)->start);
    h.undo();
    std::printf("撤销 1 次后     ：片段数 %zu，#3 回到 %dms\n", tl.clips.size(), tl.find(3)->start);
}
```

```text
120 次拖动合并后：栈深 1，#3 位于 9200ms
按 1 次 Ctrl+Z  ：#3 回到 8000ms
波纹删除 #2 后  ：片段数 2，#3 位于 3000ms
撤销 1 次后     ：片段数 3，#3 回到 8000ms
```

### 3.2 命令队列与操作日志

命令既然是对象，除了放进撤销栈，还有两个高频用法：

- **命令队列（后台任务 / 线程池）**：把转码任务包装成 `std::function<void()>` 提交到 `deque + mutex + condition_variable`，界面线程提交不到 1 µs 就返回，后台线程在**锁外**执行（实测 2 线程跑 7 个 30 ms 任务从 210 ms 降到 120 ms，完整代码见 `projects/dp-lab/20-command/job_queue.cpp`）。注意：取消只能删还在排队的任务，入队 lambda 一律按值捕获。
- **操作日志（崩溃恢复 / WAL / AOF）**：每条命令序列化成一行纯文本追加到日志（浮点数用 `%.17g` 保证精度不丢），崩溃后读上次检查点工程文件再重放日志即可恢复（前提是命令具有确定性）。实测 1000 条命令的日志仅 **22582 字节**（平均 22.6 字节/条），重放耗时 Clang 1.14 ms / GCC 0.19 ms（完整代码见 `projects/dp-lab/20-command/journal.cpp`）。FFmpeg 的 `asendcmd` 滤镜也是文本命令，如 `5.0 volume volume 0.1` 在第 5 秒将 `bbb-10s.mp4` 音频电平从 -32.6 dB 精准降到 -52.1 dB。

<figure class="diagram">
<svg viewBox="0 0 800 180" role="img" aria-label="上面一行：最左边是上次保存的工程文件，后面跟着五行操作日志：move、vol、filter、split、undo，最右边是崩溃。下面一条箭头：恢复时先读工程文件，再按顺序重放日志"><g style="font-family:var(--mono)"><text x="20.0" y="22.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">工程文件（检查点）</text><rect x="20" y="32" width="130" height="46" rx="5" style="fill:var(--panel);stroke:var(--link);stroke-width:1.4"/><text x="85.0" y="53.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px;font-weight:700">project.json</text><text x="85.0" y="68.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">上次保存的完整状态</text><text x="180.0" y="22.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">操作日志：保存之后的每条命令，一行一条，追加写</text><rect x="180" y="32" width="106" height="46" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="233.0" y="53.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">move 3 9000</text><text x="233.0" y="68.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">第 1 行</text><rect x="292" y="32" width="106" height="46" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="345.0" y="53.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">vol 2 0.3…</text><text x="345.0" y="68.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">第 2 行</text><rect x="404" y="32" width="106" height="46" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="457.0" y="53.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">filter 1 fade</text><text x="457.0" y="68.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">第 3 行</text><rect x="516" y="32" width="106" height="46" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="569.0" y="53.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">split 2 5000 4</text><text x="569.0" y="68.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">第 4 行</text><rect x="628" y="32" width="106" height="46" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="681.0" y="53.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">undo</text><text x="681.0" y="68.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">第 5 行</text><text x="768.0" y="62.0" text-anchor="middle" style="fill:var(--danger);font-size:20px;font-weight:700">✕</text><text x="768.0" y="92.0" text-anchor="middle" style="fill:var(--danger);font-size:11px">崩溃</text><text x="20.0" y="124.0" text-anchor="start" style="fill:var(--fg);font-size:11px">恢复：读检查点 → 用 parse() 把日志一行行还原成命令 → 依次 execute（遇到 undo / redo 就调 History）</text><line x1="85.0" y1="80.0" x2="85.0" y2="131.0" style="stroke:var(--accent);stroke-width:1.4"/><path d="M85.0,140.0 L90.0,131.0 L80.0,131.0 z" style="fill:var(--accent);stroke:var(--accent);stroke-width:1.2"/><line x1="85.0" y1="140.0" x2="741.0" y2="140.0" style="stroke:var(--accent);stroke-width:1.4"/><path d="M750.0,140.0 L741.0,135.0 L741.0,145.0 z" style="fill:var(--accent);stroke:var(--accent);stroke-width:1.2"/><text x="20.0" y="166.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">保存工程时：写新的检查点，清空日志。日志只需要覆盖「上次保存到崩溃」这一段</text></g></svg>
</figure>

## 4. 现代 C++ 写法与性能

### 4.1 一对 lambda 与 `std::variant`

除了虚函数类，现代 C++ 还有两种省掉样板代码的写法：**一对 `std::function`（lambda）**（开源剪辑软件 Kdenlive 的做法）和 **`std::variant` 值语义命令**（零堆分配、易于序列化）：

```cpp
// 《命令模式》：现代 C++ 两种无继承写法——一对 lambda（Action）与 std::variant
#include <cstdio>
#include <functional>
#include <variant>
#include <vector>

struct Clip { int id, start; double volume; };
struct Timeline {
    std::vector<Clip> clips;
    Clip& find(int id) { for (auto& c : clips) if (c.id == id) return c; return clips.front(); }
};

// ① 一对 lambda：不用定义命令类，捕获 id 而非 Clip&
struct Action { std::function<void()> execute, undo; };
Action makeSetVolume(Timeline& tl, int id, double v) {
    double old = tl.find(id).volume;
    return {[&tl, id, v] { tl.find(id).volume = v; }, [&tl, id, old] { tl.find(id).volume = old; }};
}

// ② std::variant：命令是纯值，零堆分配，连续存放在 vector 里
struct Move   { int id, to, from = 0; };
struct Volume { int id; double v, old = 0; };
using Cmd = std::variant<Move, Volume>;

template <class... F> struct overloaded : F... { using F::operator()...; };
template <class... F> overloaded(F...) -> overloaded<F...>;

void exec(Timeline& tl, Cmd& c) {
    std::visit(overloaded{
        [&](Move& m)   { auto& x = tl.find(m.id); m.from = x.start; x.start = m.to; },
        [&](Volume& v) { auto& x = tl.find(v.id); v.old = x.volume; x.volume = v.v; }}, c);
}
void undo(Timeline& tl, const Cmd& c) {
    std::visit(overloaded{
        [&](const Move& m)   { tl.find(m.id).start = m.from; },
        [&](const Volume& v) { tl.find(v.id).volume = v.old; }}, c);
}

int main() {
    Timeline tl{{{1, 0, 1.0}, {2, 3000, 1.0}, {3, 8000, 1.0}}};

    Action a = makeSetVolume(tl, 2, 0.5);
    a.execute(); std::printf("lambda 执行后：#2 音量 %.1f", tl.find(2).volume);
    a.undo();    std::printf(" → 撤销后：%.1f\n", tl.find(2).volume);

    std::vector<Cmd> hist = {Move{3, 9000}, Volume{2, 0.3}};
    for (auto& c : hist) exec(tl, c);
    std::printf("variant 执行后：#3 在 %dms，#2 音量 %.1f\n", tl.find(3).start, tl.find(2).volume);
    for (auto it = hist.rbegin(); it != hist.rend(); ++it) undo(tl, *it);
    std::printf("variant 撤销后：#3 在 %dms，#2 音量 %.1f\n", tl.find(3).start, tl.find(2).volume);
}
```

```text
lambda 执行后：#2 音量 0.5 → 撤销后：1.0
variant 执行后：#3 在 9000ms，#2 音量 0.3
variant 撤销后：#3 在 8000ms，#2 音量 1.0
```

### 4.2 悬空捕获的坑

lambda 命令最容易踩的坑是**按引用捕获了比命令先死的对象**（`projects/dp-lab/20-command/dangling_capture.cpp`）：

<!-- run: off -->
```cpp
Clip& c = *tl.find(id);                           // 指向 vector 里的元素
Action a{[&c, v] { c.volume = v; }, [&c, old] { c.volume = old; }};
tl.clips.push_back({4, ...});                     // vector 扩容搬家，旧内存释放
a.undo();                                         // 写入已释放内存：不加 ASan 静默失败，加 ASan 报 heap-use-after-free
```

同理：往队列里投递命令不要用 `[&]` 捕获局部变量；命令存进撤销栈不要捕获 `this`；捕获 `shared_ptr` 则可能导致被删对象滞留在无上限的撤销栈里造成内存泄漏。

### 4.3 三种写法开销实测

`projects/dp-lab/20-command/modern.cpp` 与 `bench_forms.cpp` 实测 100 万条命令（`-O2`，7 轮中位数）：
- **每条命令内存**：`variant` **32 字节（0 次堆分配）**；`unique_ptr<Command>` **40 字节（1 次堆分配）**；两个 `std::function` **128 字节 Clang / 112 字节 GCC（2 次堆分配**，因捕获超 SBO 缓冲）。

<figure class="diagram">
<svg viewBox="0 0 856 156" role="img" aria-label="每条命令的开销。执行加入栈：Clang 虚函数类 7.88 ns，两个 std::function 9.83，variant 1.51；GCC 4.27、9.33、1.29。撤销加出栈：Clang 5.73、6.24、1.15；GCC 4.62、5.85、0.74。每条占的内存：40、128（GCC 112）、32 字节"><g style="font-family:var(--mono)"><text x="158.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">执行+入栈 · Clang</text><rect x="158.0" y="30.0" width="64.5" height="20.0" rx="2" style="fill:var(--link)"/><text x="228.5" y="45.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">7.88 ns</text><rect x="158.0" y="60.0" width="80.4" height="20.0" rx="2" style="fill:var(--warn)"/><text x="244.4" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">9.83 ns</text><rect x="158.0" y="90.0" width="12.4" height="20.0" rx="2" style="fill:var(--accent)"/><text x="176.4" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">1.51 ns</text><text x="332.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">执行+入栈 · GCC</text><rect x="332.0" y="30.0" width="34.9" height="20.0" rx="2" style="fill:var(--link)"/><text x="372.9" y="45.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">4.27 ns</text><rect x="332.0" y="60.0" width="76.3" height="20.0" rx="2" style="fill:var(--warn)"/><text x="414.3" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">9.33 ns</text><rect x="332.0" y="90.0" width="10.6" height="20.0" rx="2" style="fill:var(--accent)"/><text x="348.6" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">1.29 ns</text><text x="506.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">撤销+出栈 · Clang</text><rect x="506.0" y="30.0" width="46.9" height="20.0" rx="2" style="fill:var(--link)"/><text x="558.9" y="45.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">5.73 ns</text><rect x="506.0" y="60.0" width="51.1" height="20.0" rx="2" style="fill:var(--warn)"/><text x="563.1" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">6.24 ns</text><rect x="506.0" y="90.0" width="9.4" height="20.0" rx="2" style="fill:var(--accent)"/><text x="521.4" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">1.15 ns</text><text x="680.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">撤销+出栈 · GCC</text><rect x="680.0" y="30.0" width="37.8" height="20.0" rx="2" style="fill:var(--link)"/><text x="723.8" y="45.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">4.62 ns</text><rect x="680.0" y="60.0" width="47.9" height="20.0" rx="2" style="fill:var(--warn)"/><text x="733.9" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">5.85 ns</text><rect x="680.0" y="90.0" width="6.1" height="20.0" rx="2" style="fill:var(--accent)"/><text x="692.1" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.74 ns</text><text x="150.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">unique_ptr&lt;Command&gt;</text><text x="150.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">两个 std::function</text><text x="150.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">variant + visit</text><text x="10.0" y="148.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench_forms：-O2，100 万条命令（移动和调音量交替），7 轮中位数</text></g></svg>
</figure>

选型原则：命令集合封闭且重视内存/序列化选 `variant`；需要插件动态扩展选虚函数类；不需要撤销的按键绑定或任务队列直接用 `std::function<void()>`。

## 5. 命令和相似的模式

| 模式 | 封装的是什么 | 与命令的区别 / 协作 |
| --- | --- | --- |
| **策略** | 一种算法（怎么做） | 策略长期持有、反复调用；命令代表**一次请求**（做什么 + 参数），执行后入栈或记日志 |
| **备忘录** | 某一时刻的状态快照 | 备忘录存状态、命令存操作；两者常结合（如 `DeleteClip` 内部存一份被删片段的备忘录 `saved_`） |
| **责任链** | 传递请求的处理链 | 命令是被传递的对象，责任链是传递的路径（如 UI 事件沿焦点链冒泡） |
| **组合** | 树形部分-整体结构 | 宏命令就是组合模式用在命令上 |

## 6. 小结

- 命令模式把「一次操作」变成对象，从而支持撤销/重做、宏命令、操作合并、后台排队与操作日志。
- 实测 200 片段 1000 次操作：整份快照 34368 KB、共享快照 3344 KB、命令仅 **47 KB**。
- 写可撤销命令要牢记：`execute()` 里记旧值、撤销恢复旧值而非做反向运算、存 id 不存指针、新对象 id 在构造时定好、宏命令倒序撤销、做新命令清空重做栈。
- 现代 C++ 中 `std::variant` 零堆分配最快（1.51 ns），lambda 写起来最轻但切忌按引用捕获短命对象或 `vector` 元素。

## 7. 练习

1. **辨析**：下面四种情况，哪些属于命令模式？不是的话更像哪种模式？
   - (a) 播放器把「暂停」「seek 到 30 s」「切字幕轨」做成对象放进队列，由播放线程在帧间依次执行；
   - (b) `std::sort` 传入的比较器 lambda；
   - (c) 剪辑器每 5 分钟把整份工程序列化，提供「恢复到 10 分钟前」；
   - (d) 快捷键映射表 `unordered_map<string, function<void()>>`，允许用户改绑快捷键。
2. **设计**：给 `History` 增加两个工程必备功能：① 撤销历史最多保留 `N` 步，超出丢弃最老的；② 记录「保存点」（标题栏未保存显示 `*`，撤销回保存点 `*` 消失）。思考：保存后撤销一步再做新命令，或者保存点被步数上限挤掉后，该怎么处理？
3. **找问题**：下面这个「音量 +delta」命令在撤销后结果不对，问题出在哪？怎么改？

```cpp
// 《命令模式》练习 3：执行时做了 clamp 截断，撤销时直接减回 delta，结果出错
#include <algorithm>
#include <cstdio>

struct Clip { double volume = 1.0; };

class AdjustVolume {
public:
    AdjustVolume(Clip& c, double delta) : c_(c), delta_(delta) {}
    void execute() { c_.volume = std::clamp(c_.volume + delta_, 0.0, 2.0); }
    void undo() { c_.volume -= delta_; }
private:
    Clip& c_;
    double delta_;
};

int main() {
    Clip clip{1.5};
    AdjustVolume up(clip, 1.0);
    up.execute();
    std::printf("1.5 加 1.0：%.1f（限制在 2.0）\n", clip.volume);
    up.undo();
    std::printf("撤销：%.1f（应该是 1.5）\n", clip.volume);
}
```

<details>
<summary>点击查看答案</summary>

1. (a) **命令**（请求对象化并排队跨线程执行，不要求必须支持撤销）；
   (b) **策略**（「怎么比」的算法）；
   (c) **备忘录/快照**（存状态）；
   (d) **命令**（将调用者按键与具体操作解耦）。

2. 把撤销栈 `done_` 换成 `std::deque` 以支持 `pop_front()`，并用 `long clean_` 记录保存时 `done_.size()`（`isClean()` 判断 `clean_ == (long)done_.size()`）：
   - 做新命令前若 `clean_ > (long)done_.size()`，说明保存点在即将被清空的重做栈里，置 `clean_ = -1`（再也回不去）；
   - 超出上限 `done_.pop_front()` 时，若 `clean_ >= 0` 则 `--clean_`（从 0 减到 -1 同样表示回不去）。
   完整实现见 `projects/dp-lab/20-command/ex3_limit.cpp`（另见 `ex2_trim.cpp` 的 `TrimClip` 合并练习）。

3. `execute()` 里有 `std::clamp` 截断，1.5 + 1.0 被截成 2.0，撤销时用反向运算 `volume -= 1.0` 会变成 1.0。凡是带截断、取整或浮点累加的操作都不能靠反向运算撤销，必须在 `execute()` 里记录 `old_ = c_.volume`，`undo()` 直接恢复 `c_.volume = old_`；另外实际放入撤销栈时应存片段 id 而非 `Clip&`（修复版见 `projects/dp-lab/20-command/ex4_fixed.cpp`）。

</details>
