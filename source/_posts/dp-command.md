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

> **本文目标**：理解命令模式：把「要做的一件事」封装成对象，调用者只认识 `Command` 接口；会写带 `execute / undo` 的命令和撤销栈、重做栈，知道撤销后做新命令为什么要清空重做栈；会写宏命令和命令合并；知道命令还能排队（后台任务）、写成日志（崩溃恢复）；量出整份快照、共享快照、命令三种撤销方式的内存和耗时；会用 lambda 和 `std::variant` 写命令，避开按引用捕获的悬空坑；分清命令、策略、备忘录、责任链。
>
> **前置知识**：上一篇《观察者模式》；《策略模式》（命令和策略长得很像，第 5 节对比）；《组合模式》（宏命令就是命令的组合）；C/C++ 专栏的 lambda 和 `std::function`。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。完整代码在仓库的 `projects/dp-lab/20-command/` 目录，`sh measure.sh` 用 `-O2` 跑一遍本文的实测，并用 AddressSanitizer 跑悬空捕获的版本。

## 1. 问题：给剪辑器加上撤销

### 1.1 直接改状态的剪辑器

做一个最简单的视频剪辑器：时间线上排着几个片段（都剪自测试视频 `bbb-10s.mp4`），可以移动片段、调音量、加滤镜、在某个时间点把片段剪成两段。最直接的写法是剪辑器的每个方法直接改时间线：

<!-- run: off -->
```cpp
struct Clip {
    int id;
    std::string source;              // 素材文件
    int start, in, out;              // 时间线上的起点；素材里的入点、出点（毫秒）
    double volume = 1.0;
    std::vector<std::string> filters;
};

class Editor {
public:
    void move(int id, int start)          { tl_.find(id)->start = start; }
    void setVolume(int id, double v)      { tl_.find(id)->volume = v; }
    void addFilter(int id, std::string f) { tl_.find(id)->filters.push_back(std::move(f)); }
    void split(int id, int at);           // 剪成两段
private:
    Timeline tl_;
};

// 菜单、快捷键、时间线上的拖动，都直接调用这些方法
editor.move(3, 9000);
```

能用，直到产品经理说「要支持 Ctrl+Z」。问题来了：`move(3, 9000)` 执行完就结束了，**片段原来在哪，没有人记得**。`setVolume` 也一样，旧音量被直接覆盖。操作是一次函数调用，调完就没了，没有一个「东西」可以拿来撤销。

### 1.2 只能存快照

不改操作的写法，唯一的办法是：每次改之前，把整条时间线复制一份存起来，撤销时换回去。

```cpp
// 《命令模式》：直接改状态的剪辑器，想撤销只能每次存一份整条时间线的快照，内存随操作数线性增长
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

// ---- 统计堆上还活着的字节数（替换全局 operator new / delete）----
static std::size_t g_live = 0;
void* operator new(std::size_t n) {
    auto* p = static_cast<std::size_t*>(std::malloc(n + 16));
    if (!p) std::abort();
    *p = n;
    g_live += n;
    return reinterpret_cast<char*>(p) + 16;
}
void operator delete(void* p) noexcept {
    if (!p) return;
    auto* h = reinterpret_cast<std::size_t*>(static_cast<char*>(p) - 16);
    g_live -= *h;
    std::free(h);
}
void operator delete(void* p, std::size_t) noexcept { operator delete(p); }

struct Clip {
    int id;
    std::string source;              // 素材文件
    int start, in, out;              // 时间线上的起点；素材里的入点、出点（毫秒）
    double volume = 1.0;
    std::vector<std::string> filters;
};

struct Timeline {
    std::vector<Clip> clips;
    Clip* find(int id) {
        for (auto& c : clips)
            if (c.id == id) return &c;
        return nullptr;
    }
};

// 剪辑器：每个操作直接改时间线。它不知道「改之前是什么样」，
// 想撤销，只能在每次改之前把整条时间线复制一份
class Editor {
public:
    explicit Editor(Timeline& tl) : tl_(tl) {}
    void move(int id, int start)          { save(); tl_.find(id)->start = start; }
    void setVolume(int id, double v)      { save(); tl_.find(id)->volume = v; }
    void addFilter(int id, std::string f) { save(); tl_.find(id)->filters.push_back(std::move(f)); }
    bool undo() {
        if (history_.empty()) return false;
        tl_ = std::move(history_.back());
        history_.pop_back();
        return true;
    }
    std::size_t steps() const { return history_.size(); }

private:
    void save() { history_.push_back(tl_); }   // 整条时间线的快照
    Timeline& tl_;
    std::vector<Timeline> history_;
};

int main() {
    Timeline tl;
    for (int i = 0; i < 200; ++i)    // 200 个片段，都剪自 bbb-10s.mp4，每个带一个缩放滤镜
        tl.clips.push_back({i + 1, "bbb-10s.mp4", i * 2000, i * 40 % 8000, i * 40 % 8000 + 2000, 1.0,
                            {"scale=1920:1080:flags=lanczos"}});
    std::size_t base = g_live;
    std::printf("时间线本身：%zu 个片段，堆上 %.1f KB\n", tl.clips.size(), base / 1024.0);

    Editor ed(tl);
    int done = 0;
    for (int target : {1, 10, 100, 1000}) {
        for (; done < target; ++done) {
            int id = done * 37 % 200 + 1;
            if (done % 2 == 0) ed.move(id, tl.find(id)->start + 10);
            else ed.setVolume(id, 0.1 * (done % 7 + 1));
        }
        std::printf("操作 %4d 次：撤销历史 %4zu 份快照，多占 %8.1f KB\n", done, ed.steps(),
                    (g_live - base) / 1024.0);
    }
    int id = 999 * 37 % 200 + 1;
    std::printf("最后一步把 #%d 的音量改成了 %.1f\n", id, tl.find(id)->volume);
    ed.undo();
    std::printf("撤销一步：#%d 的音量回到 %.1f\n", id, tl.find(id)->volume);
}
```

```text
时间线本身：200 个片段，堆上 30.9 KB
操作    1 次：撤销历史    1 份快照，多占     26.6 KB
操作   10 次：撤销历史   10 份快照，多占    266.0 KB
操作  100 次：撤销历史  100 份快照，多占   2659.2 KB
操作 1000 次：撤销历史 1000 份快照，多占  26586.5 KB
最后一步把 #164 的音量改成了 0.6
撤销一步：#164 的音量回到 0.2
```

撤销确实能用，但每一步都存了一整条时间线：**每次只改了一个片段的一个字段，却复制了 200 个片段**。1000 步就是 26 MB，而且和时间线的大小成正比：片段数翻倍，内存也翻倍。GCC 上同样是 1000 步 29321 KB，比 Clang 多一点，因为 libstdc++ 的 `std::string` 是 32 字节（libc++ 是 24 字节）。真实的剪辑工程里，片段还带着关键帧、转场、字幕，一份快照比这大得多。

### 1.3 问题在哪

内存只是最显眼的一个。更根本的问题是：**操作没有变成一个对象**。所以下面这些事都做不了，或者很别扭：

1. **菜单上显示「撤销 移动片段」**：快照里只有状态，不知道是哪个操作产生的。
2. **合并**：拖动一个片段会产生上百个 mousemove 事件，每个都存一份快照，用户要按上百次 Ctrl+Z。
3. **一次撤销好几个操作**：「波纹删除」= 删除一个片段 + 后面的片段整体左移，用户希望一次撤销。
4. **排队、放到后台去做**：转码导出要好几分钟，界面不能等。
5. **记下来**：崩溃之后，想知道用户最后做了哪些操作，重放一遍。
6. **每个入口都要记得先 `save()`**：菜单、快捷键、拖动、脚本……漏掉一个，那个操作就撤销不了。

## 2. 命令模式

### 2.1 意图

GoF 的定义：把一个请求封装成一个对象，从而可以用不同的请求把客户参数化、把请求排队或者记录日志，以及支持可撤销的操作。

说成大白话：**把「要做的一件事」变成一个对象**。这个对象知道做什么（移动哪个片段、移到哪），能执行，还能撤回自己做过的事。有了对象，就能存起来、排队、合并、写进文件——一次函数调用做不到的事，一个对象都能做。

### 2.2 用命令模式重构

每个操作写成一个类，实现同一个接口：`execute()` 做事并顺手记下旧值，`undo()` 用旧值恢复。`History` 维护两个栈：做过的命令（撤销栈）和撤销掉的命令（重做栈）。

```cpp
// 《命令模式》：把剪辑操作封装成命令对象，每个命令自己记住怎么撤销；History 维护撤销栈和重做栈
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Clip {
    int id;
    std::string source;
    int start, in, out;              // 时间线上的起点；素材里的入点、出点（毫秒）
    double volume = 1.0;
    std::vector<std::string> filters;
    int end() const { return start + out - in; }
};

struct Timeline {                    // 接收者（Receiver）：真正被改的对象
    std::vector<Clip> clips;
    Clip* find(int id) {
        for (auto& c : clips)
            if (c.id == id) return &c;
        return nullptr;
    }
    int indexOf(int id) const {
        for (int i = 0; i < (int)clips.size(); ++i)
            if (clips[i].id == id) return i;
        return -1;
    }
};

class Command {
public:
    virtual ~Command() = default;
    virtual void execute(Timeline& tl) = 0;
    virtual void undo(Timeline& tl) = 0;
    virtual std::string name() const = 0;
};

class MoveClip : public Command {
public:
    MoveClip(int id, int to) : id_(id), to_(to) {}
    void execute(Timeline& tl) override {
        Clip* c = tl.find(id_);
        from_ = c->start;            // 执行时记下旧值，撤销时用
        c->start = to_;
    }
    void undo(Timeline& tl) override { tl.find(id_)->start = from_; }
    std::string name() const override { return "移动 #" + std::to_string(id_); }

private:
    int id_, to_, from_ = 0;
};

class SetVolume : public Command {
public:
    SetVolume(int id, double v) : id_(id), v_(v) {}
    void execute(Timeline& tl) override {
        Clip* c = tl.find(id_);
        old_ = c->volume;
        c->volume = v_;
    }
    void undo(Timeline& tl) override { tl.find(id_)->volume = old_; }
    std::string name() const override { return "调音量 #" + std::to_string(id_); }

private:
    int id_;
    double v_, old_ = 0;
};

class AddFilter : public Command {
public:
    AddFilter(int id, std::string f) : id_(id), f_(std::move(f)) {}
    void execute(Timeline& tl) override { tl.find(id_)->filters.push_back(f_); }
    void undo(Timeline& tl) override { tl.find(id_)->filters.pop_back(); }
    std::string name() const override { return "加滤镜 #" + std::to_string(id_); }

private:
    int id_;
    std::string f_;
};

// 剪切：在时间线上的 at 处把片段一分为二，后半段的 id 在构造时就定好，重做时还是这个 id
class SplitClip : public Command {
public:
    SplitClip(int id, int at, int newId) : id_(id), at_(at), newId_(newId) {}
    void execute(Timeline& tl) override {
        int i = tl.indexOf(id_);
        Clip right = tl.clips[i];
        int cut = tl.clips[i].in + (at_ - tl.clips[i].start);
        right.id = newId_;
        right.start = at_;
        right.in = cut;
        tl.clips[i].out = cut;
        tl.clips.insert(tl.clips.begin() + i + 1, right);
    }
    void undo(Timeline& tl) override {
        int r = tl.indexOf(newId_);
        tl.find(id_)->out = tl.clips[r].out;
        tl.clips.erase(tl.clips.begin() + r);
    }
    std::string name() const override { return "剪切 #" + std::to_string(id_); }

private:
    int id_, at_, newId_;
};

class History {                      // 调用者（Invoker）：只认识 Command 接口
public:
    explicit History(Timeline& tl) : tl_(tl) {}
    void run(std::unique_ptr<Command> c) {
        c->execute(tl_);
        done_.push_back(std::move(c));
        undone_.clear();             // 做了新操作，重做栈作废
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
        std::printf("   撤销栈：");
        for (auto& c : done_) std::printf("[%s] ", c->name().c_str());
        std::printf("\n   重做栈：");
        for (auto& c : undone_) std::printf("[%s] ", c->name().c_str());
        std::printf("\n");
    }

private:
    Timeline& tl_;
    std::vector<std::unique_ptr<Command>> done_, undone_;
};

void show(const Timeline& tl) {
    for (auto& c : tl.clips) {
        std::printf("   #%d %5d-%5dms 音量 %.1f", c.id, c.start, c.end(), c.volume);
        for (auto& f : c.filters) std::printf(" [%s]", f.c_str());
        std::printf("\n");
    }
}

int main() {
    Timeline tl{{{1, "bbb-10s.mp4", 0, 0, 3000, 1.0, {} },
                 {2, "bbb-10s.mp4", 3000, 3000, 8000, 1.0, {} },
                 {3, "bbb-10s.mp4", 8000, 8000, 10000, 1.0, {} }}};
    History h(tl);
    h.run(std::make_unique<MoveClip>(3, 9000));
    h.run(std::make_unique<SetVolume>(2, 0.5));
    h.run(std::make_unique<AddFilter>(1, "fade=t=in:d=1"));
    h.run(std::make_unique<SplitClip>(2, 5000, 4));
    std::printf("执行 4 条命令：\n");
    show(tl);
    h.print();

    h.undo();
    h.undo();
    std::printf("撤销两次（先撤剪切，再撤滤镜）：\n");
    show(tl);
    h.print();

    h.redo();
    std::printf("重做一次（滤镜回来了）：\n");
    show(tl);

    h.run(std::make_unique<SetVolume>(1, 0.8));
    std::printf("再做一条新命令，重做栈被清空，redo() 返回 %s：\n", h.redo() ? "true" : "false");
    h.print();
}
```

```text
执行 4 条命令：
   #1     0- 3000ms 音量 1.0 [fade=t=in:d=1]
   #2  3000- 5000ms 音量 0.5
   #4  5000- 8000ms 音量 0.5
   #3  9000-11000ms 音量 1.0
   撤销栈：[移动 #3] [调音量 #2] [加滤镜 #1] [剪切 #2] 
   重做栈：
撤销两次（先撤剪切，再撤滤镜）：
   #1     0- 3000ms 音量 1.0
   #2  3000- 8000ms 音量 0.5
   #3  9000-11000ms 音量 1.0
   撤销栈：[移动 #3] [调音量 #2] 
   重做栈：[剪切 #2] [加滤镜 #1] 
重做一次（滤镜回来了）：
   #1     0- 3000ms 音量 1.0 [fade=t=in:d=1]
   #2  3000- 8000ms 音量 0.5
   #3  9000-11000ms 音量 1.0
再做一条新命令，重做栈被清空，redo() 返回 false：
   撤销栈：[移动 #3] [调音量 #2] [加滤镜 #1] [调音量 #1] 
   重做栈：
```

几个细节值得注意：

- **旧值在 `execute()` 里记，不在构造函数里记**。重做时会再调用一次 `execute()`，那时时间线的状态可能和第一次执行时不一样（比如中间撤销过别的命令），必须重新记。
- **命令里存片段的 id，不存 `Clip*`**。`SplitClip` 会往 `vector` 里插入元素，插入可能让 `vector` 扩容，之前拿到的指针全部失效；`DeleteClip` 撤销时插回去的是一个新对象，地址也变了。id 不会变。
- **新片段的 id 在构造命令时定好**（`SplitClip(2, 5000, 4)` 里的 `4`），而不是在 `execute()` 里临时分配。否则撤销再重做，后半段可能拿到一个新 id，撤销栈里后面引用 `#4` 的命令就找不到它了。
- **撤销之后做新命令，重做栈要清空**。被撤销的命令是在旧状态上记下的旧值，在新命令改过的状态上重做，结果没有意义。所以撤销历史是一条线，不是一棵树（Vim 的撤销树是另一种设计：撤销后做新操作，旧的那一支还留着，代价是用户很难看懂）。

### 2.3 类图和时序图

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

和类图对照着看：

- **Command（`Command`）**：命令接口，`execute()` 和 `undo()`。
- **ConcreteCommand（`MoveClip` 等）**：一个具体的操作，自己保存参数（移到哪）和撤销需要的旧值（原来在哪）。
- **Receiver（`Timeline`）**：真正被改的对象。命令执行时去改它。
- **Invoker（`History`）**：调用命令的一方。它只认识 `Command` 接口，不知道有哪些具体命令；加一种新操作，`History` 一行不用改。
- **Client（时间线界面、快捷键处理）**：创建具体命令，交给 `History`。

GoF 原书里，具体命令在构造时就持有接收者的引用。这里改成 `execute(Timeline&)` 时再传进来：命令只剩下纯数据（id 和数值），不会悬空，也很容易写进文件（第 3.4 节）。两种都常见，第 4 节的 lambda 写法就是持有接收者的那一种。

按一次 Ctrl+Z 的时序：

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

界面只认识 `History`，`History` 只认识 `Command`。撤销具体怎么做，只有 `SplitClip` 自己知道。

### 2.4 看两个栈怎么动

下面的演示里，上面是时间线，下面是两个栈。四个按钮分别演示：执行、撤销、重做；撤销后做新命令（重做栈被清空）；拖动时的命令合并（第 3.2 节）；宏命令倒着撤销（第 3.1 节）。

<div class="dsviz" data-viz="dp-command"></div>

### 2.5 实测：三种撤销方式

<figure class="diagram">
<svg viewBox="0 0 720 290" role="img" aria-label="左边是快照栈：四格，每格都是一整条时间线的十个片段，只有一个片段和上一格不同。右边是命令栈：四格，每格一行，比如 MoveClip #3 from=8000 to=9000，只记改了什么和原来的值"><g style="font-family:var(--mono)"><text x="20.0" y="20.0" text-anchor="start" style="fill:var(--warn);font-size:12.5px;font-weight:700">整份快照：每一格是整条时间线</text><text x="400.0" y="20.0" text-anchor="start" style="fill:var(--accent);font-size:12.5px;font-weight:700">命令：每一格只记「改了什么、原来是什么」</text><rect x="20" y="40" width="330" height="42" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><rect x="28.0" y="48.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="60.0" y="48.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="92.0" y="48.0" width="28.0" height="26.0" rx="3" style="fill:var(--warn)"/><rect x="124.0" y="48.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="156.0" y="48.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="188.0" y="48.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="220.0" y="48.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="252.0" y="48.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="284.0" y="48.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="316.0" y="48.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><text x="340.0" y="66.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px"></text><rect x="20" y="92" width="330" height="42" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><rect x="28.0" y="100.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="60.0" y="100.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="92.0" y="100.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="124.0" y="100.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="156.0" y="100.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="188.0" y="100.0" width="28.0" height="26.0" rx="3" style="fill:var(--warn)"/><rect x="220.0" y="100.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="252.0" y="100.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="284.0" y="100.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="316.0" y="100.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><text x="340.0" y="118.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px"></text><rect x="20" y="144" width="330" height="42" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><rect x="28.0" y="152.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="60.0" y="152.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="92.0" y="152.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="124.0" y="152.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="156.0" y="152.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="188.0" y="152.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="220.0" y="152.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="252.0" y="152.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="284.0" y="152.0" width="28.0" height="26.0" rx="3" style="fill:var(--warn)"/><rect x="316.0" y="152.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><text x="340.0" y="170.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px"></text><rect x="20" y="196" width="330" height="42" rx="4" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.2"/><rect x="28.0" y="204.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="60.0" y="204.0" width="28.0" height="26.0" rx="3" style="fill:var(--warn)"/><rect x="92.0" y="204.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="124.0" y="204.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="156.0" y="204.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="188.0" y="204.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="220.0" y="204.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="252.0" y="204.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="284.0" y="204.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><rect x="316.0" y="204.0" width="28.0" height="26.0" rx="3" style="fill:var(--border-2)"/><text x="340.0" y="222.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px"></text><text x="20.0" y="258.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">200 个片段 × 1000 步 ≈ 34 MB</text><text x="20.0" y="276.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">每格里只有 1 个片段（橙色）和上一格不同</text><rect x="400" y="46" width="300" height="30" rx="5" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.4"/><text x="550.0" y="65.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">MoveClip  #3  from=8000  to=9000</text><rect x="400" y="98" width="300" height="30" rx="5" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.4"/><text x="550.0" y="117.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">SetVolume #2  old=1.0  v=0.5</text><rect x="400" y="150" width="300" height="30" rx="5" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.4"/><text x="550.0" y="169.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">AddFilter #1  "fade=t=in:d=1"</text><rect x="400" y="202" width="300" height="30" rx="5" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.4"/><text x="550.0" y="221.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">SplitClip #2  at=5000  newId=4</text><text x="400.0" y="258.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">1000 步 ≈ 47 KB：每格几十字节</text></g></svg>
</figure>

除了整份快照和命令，还有一种折中：**共享快照**。片段对象做成不可变的，时间线存 `shared_ptr<const Clip>`；存快照只复制 200 个指针，改哪个片段就复制哪个片段，别的片段新旧快照共用：

<!-- run: off -->
```cpp
struct TimelineB {
    std::vector<std::shared_ptr<const Clip>> clips;
    void modify(int id, int k) {
        auto it = std::find_if(clips.begin(), clips.end(), [&](auto& c) { return c->id == id; });
        auto copy = std::make_shared<Clip>(**it);   // 只复制被改的这一个
        applyOp(*copy, k);
        *it = std::move(copy);                      // 旧的那个还被历史里的快照引用着
    }
};
// 每次操作：hist.push_back(tl); tl.modify(id, k);
```

`bench_undo.cpp` 用 200 个片段的时间线，做 1000 次操作（移动、调音量、加滤镜轮流），三种方式各跑 7 轮取中位数：

<figure class="diagram">
<svg viewBox="0 0 740 156" role="img" aria-label="200 个片段的时间线做 1000 次操作后，撤销历史占的内存。Clang：整份快照 34368 KB，共享快照 3344 KB，命令 47 KB。GCC：37345、3353、48 KB"><g style="font-family:var(--mono)"><text x="198.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang / macOS</text><rect x="198.0" y="30.0" width="162.8" height="20.0" rx="2" style="fill:var(--danger)"/><text x="366.8" y="45.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">34368 KB</text><rect x="198.0" y="60.0" width="15.8" height="20.0" rx="2" style="fill:var(--warn)"/><text x="219.8" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">3344 KB</text><rect x="198.0" y="90.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="205.5" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">47 KB</text><text x="468.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC 13 / Ubuntu</text><rect x="468.0" y="30.0" width="176.9" height="20.0" rx="2" style="fill:var(--danger)"/><text x="650.9" y="45.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">37345 KB</text><rect x="468.0" y="60.0" width="15.9" height="20.0" rx="2" style="fill:var(--warn)"/><text x="489.9" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">3353 KB</text><rect x="468.0" y="90.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="475.5" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">48 KB</text><text x="190.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">整份快照</text><text x="190.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">共享快照（改哪个复制哪个）</text><text x="190.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">命令</text><text x="10.0" y="148.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench_undo：-O2，1000 次操作后把历史销毁，统计释放的堆内存；7 轮中位数</text></g></svg>
</figure>

<figure class="diagram">
<svg viewBox="0 0 896 156" role="img" aria-label="每次操作的耗时（包括存快照或创建命令）：Clang 4.58、0.233、0.054 微秒；GCC 4.79、0.174、0.033。撤销全部 1000 步：Clang 2902、141、32 微秒；GCC 2422、148、13"><g style="font-family:var(--mono)"><text x="198.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">每次操作 · Clang</text><rect x="198.0" y="30.0" width="79.3" height="20.0" rx="2" style="fill:var(--danger)"/><text x="283.3" y="45.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">4.58 µs</text><rect x="198.0" y="60.0" width="4.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="208.0" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">0.233 µs</text><rect x="198.0" y="90.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="205.5" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.054 µs</text><text x="372.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">每次操作 · GCC</text><rect x="372.0" y="30.0" width="82.9" height="20.0" rx="2" style="fill:var(--danger)"/><text x="460.9" y="45.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">4.791 µs</text><rect x="372.0" y="60.0" width="3.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="381.0" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">0.174 µs</text><rect x="372.0" y="90.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="379.5" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.033 µs</text><text x="546.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">撤销 1000 步 · Clang</text><rect x="546.0" y="30.0" width="81.6" height="20.0" rx="2" style="fill:var(--danger)"/><text x="633.6" y="45.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">2901.6 µs</text><rect x="546.0" y="60.0" width="4.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="556.0" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">141.3 µs</text><rect x="546.0" y="90.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="553.5" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">32 µs</text><text x="720.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">撤销 1000 步 · GCC</text><rect x="720.0" y="30.0" width="68.1" height="20.0" rx="2" style="fill:var(--danger)"/><text x="794.1" y="45.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">2422.1 µs</text><rect x="720.0" y="60.0" width="4.2" height="20.0" rx="2" style="fill:var(--warn)"/><text x="730.2" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">148.4 µs</text><rect x="720.0" y="90.0" width="1.5" height="20.0" rx="2" style="fill:var(--accent)"/><text x="727.5" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">13.3 µs</text><text x="190.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">整份快照</text><text x="190.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">共享快照（改哪个复制哪个）</text><text x="190.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">命令</text><text x="10.0" y="148.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench_undo：-O2，7 轮中位数。快照撤销是把整条时间线 move 回来，旧的那份要析构（几百次 free）</text></g></svg>
</figure>

| | 整份快照 | 共享快照 | 命令 |
|---|---|---|---|
| 1000 步的撤销历史（Clang / GCC） | 34368 / 37345 KB | 3344 / 3353 KB | 47 / 48 KB |
| 1000 次操作的堆分配次数 | 525911 | 5011 | 1661 |
| 每次操作（Clang / GCC） | 4.58 / 4.79 µs | 0.233 / 0.174 µs | 0.054 / 0.033 µs |
| 撤销 1000 步（Clang / GCC） | 2902 / 2422 µs | 141 / 148 µs | 32 / 13 µs |

- **内存**：命令比整份快照少了 700 多倍。每条命令几十字节（对象本身 + `AddFilter` 里的滤镜字符串），和时间线有多大无关。共享快照每步约 3.3 KB：200 个 `shared_ptr` 就是 3.2 KB。
- **每次操作的耗时**：整份快照每次要复制 200 个片段，每个片段里有字符串和 `vector`，1000 次操作做了 52 万次堆分配。
- **撤销**：快照的撤销只是一次 `move`，本身很快，但被换下来的那份时间线要析构，又是几百次 `free`。命令的撤销只改一个字段，加上在 200 个片段里按 id 找一遍。
- 两边结论一致，数量级也一样。GCC 的命令版撤销（13 µs）比 Clang（32 µs）快一倍多，不过绝对值都只有几十微秒。

共享快照不是摆设：它不需要为每种操作写 `undo()`，状态本身就是历史，适合「操作种类很多、每次只改一小块」的场景，函数式语言和一些前端状态库（不可变数据 + 结构共享）就是这么做撤销的。代价是所有修改都要走「复制再替换」，而且数据结构要设计成能共享。命令模式的代价则是每种操作都要正确地写出 `undo()`——写错了不会有任何报错（练习 4）。

## 3. 深入：宏、合并、队列、日志

### 3.1 宏命令：一次撤销好几个操作

「波纹删除」是删掉一个片段，后面的片段整体往前挪，补上空出来的位置。它由好几条命令组成，但对用户是一个操作，撤销也应该一次撤掉。做法是让一组命令本身也成为一个 `Command`——这就是《组合模式》：

```cpp
// 《命令模式》：宏命令（波纹删除 = 删除片段 + 后面的片段整体左移）和命令合并（一次拖动的上百个移动合成一条）
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Clip {
    int id;
    int start, len;                  // 只保留这一节用得到的字段（毫秒）
};

struct Timeline {
    std::vector<Clip> clips;
    int indexOf(int id) const {
        for (int i = 0; i < (int)clips.size(); ++i)
            if (clips[i].id == id) return i;
        return -1;
    }
    Clip* find(int id) { int i = indexOf(id); return i < 0 ? nullptr : &clips[i]; }
};

class Command {
public:
    virtual ~Command() = default;
    virtual void execute(Timeline& tl) = 0;
    virtual void undo(Timeline& tl) = 0;
    virtual std::string name() const = 0;
    // 能不能把紧跟在后面的 next 并进自己；能就并进来并返回 true（next 已经执行过了）
    virtual bool mergeWith(const Command&) { return false; }
};

class MoveClip : public Command {
public:
    // gesture：同一次拖动（按下鼠标到松开）里产生的移动命令带同一个编号；0 表示不参与合并
    MoveClip(int id, int to, int gesture = 0) : id_(id), to_(to), gesture_(gesture) {}
    void execute(Timeline& tl) override {
        Clip* c = tl.find(id_);
        from_ = c->start;
        c->start = to_;
    }
    void undo(Timeline& tl) override { tl.find(id_)->start = from_; }
    std::string name() const override {
        return "移动 #" + std::to_string(id_) + " " + std::to_string(from_) + "→" + std::to_string(to_);
    }
    bool mergeWith(const Command& next) override {
        auto* m = dynamic_cast<const MoveClip*>(&next);
        if (!m || gesture_ == 0 || m->gesture_ != gesture_ || m->id_ != id_) return false;
        to_ = m->to_;                // 起点 from_ 用自己的，终点用最新的
        return true;
    }

private:
    int id_, to_, gesture_, from_ = 0;
};

class DeleteClip : public Command {
public:
    explicit DeleteClip(int id) : id_(id) {}
    void execute(Timeline& tl) override {
        index_ = tl.indexOf(id_);
        saved_ = tl.clips[index_];   // 删掉之前把整个片段存下来
        tl.clips.erase(tl.clips.begin() + index_);
    }
    void undo(Timeline& tl) override { tl.clips.insert(tl.clips.begin() + index_, saved_); }
    std::string name() const override { return "删除 #" + std::to_string(id_); }

private:
    int id_, index_ = 0;
    Clip saved_{};
};

// 宏命令：本身也是 Command，里面装着一串命令；执行按顺序，撤销倒着来
class MacroCommand : public Command {
public:
    explicit MacroCommand(std::string name) : name_(std::move(name)) {}
    void add(std::unique_ptr<Command> c) { cmds_.push_back(std::move(c)); }
    void execute(Timeline& tl) override {
        for (auto& c : cmds_) c->execute(tl);
    }
    void undo(Timeline& tl) override {
        for (auto it = cmds_.rbegin(); it != cmds_.rend(); ++it) (*it)->undo(tl);
    }
    std::string name() const override { return name_ + "（" + std::to_string(cmds_.size()) + " 条）"; }

private:
    std::string name_;
    std::vector<std::unique_ptr<Command>> cmds_;
};

// 波纹删除：删掉一个片段，后面的片段都往前挪，补上空出来的位置
std::unique_ptr<Command> rippleDelete(const Timeline& tl, int id) {
    auto macro = std::make_unique<MacroCommand>("波纹删除 #" + std::to_string(id));
    const Clip& del = tl.clips[tl.indexOf(id)];
    macro->add(std::make_unique<DeleteClip>(id));
    for (auto& c : tl.clips)
        if (c.start > del.start) macro->add(std::make_unique<MoveClip>(c.id, c.start - del.len));
    return macro;
}

class History {
public:
    explicit History(Timeline& tl) : tl_(tl) {}
    void run(std::unique_ptr<Command> c) {
        c->execute(tl_);
        undone_.clear();
        if (!done_.empty() && done_.back()->mergeWith(*c)) return;   // 并进上一条了，这条丢掉
        done_.push_back(std::move(c));
    }
    bool undo() {
        if (done_.empty()) return false;
        done_.back()->undo(tl_);
        undone_.push_back(std::move(done_.back()));
        done_.pop_back();
        return true;
    }
    std::size_t size() const { return done_.size(); }
    std::string top() const { return done_.empty() ? "（空）" : done_.back()->name(); }

private:
    Timeline& tl_;
    std::vector<std::unique_ptr<Command>> done_, undone_;
};

void show(const char* title, const Timeline& tl) {
    std::printf("%s", title);
    for (auto& c : tl.clips) std::printf(" #%d[%d,%d)", c.id, c.start, c.start + c.len);
    std::printf("\n");
}

int main() {
    // 1. 拖动：鼠标从 8000ms 拖到 9200ms，每 10ms 来一个 mousemove 事件
    for (int gesture : {0, 1}) {
        Timeline tl{{{1, 0, 3000}, {2, 3000, 5000}, {3, 8000, 2000}}};
        History h(tl);
        for (int x = 8010; x <= 9200; x += 10) h.run(std::make_unique<MoveClip>(3, x, gesture));
        std::printf("%s：拖动产生 120 条移动命令，撤销栈里有 %zu 条，栈顶是「%s」\n",
                    gesture ? "合并" : "不合并", h.size(), h.top().c_str());
        int presses = 0;
        while (tl.find(3)->start != 8000 && h.undo()) ++presses;
        std::printf("   要按 %d 次 Ctrl+Z 才能回到 8000ms\n", presses);
    }

    // 2. 宏命令：波纹删除 #2
    Timeline tl{{{1, 0, 3000}, {2, 3000, 5000}, {3, 8000, 2000}, {4, 10000, 1500}}};
    History h(tl);
    show("删除前      ：", tl);
    h.run(rippleDelete(tl, 2));
    show("波纹删除 #2 ：", tl);
    std::printf("   撤销栈 %zu 条：「%s」\n", h.size(), h.top().c_str());
    h.undo();
    show("撤销一次    ：", tl);
}
```

```text
不合并：拖动产生 120 条移动命令，撤销栈里有 120 条，栈顶是「移动 #3 9190→9200」
   要按 120 次 Ctrl+Z 才能回到 8000ms
合并：拖动产生 120 条移动命令，撤销栈里有 1 条，栈顶是「移动 #3 8000→9200」
   要按 1 次 Ctrl+Z 才能回到 8000ms
删除前      ： #1[0,3000) #2[3000,8000) #3[8000,10000) #4[10000,11500)
波纹删除 #2 ： #1[0,3000) #3[3000,5000) #4[5000,6500)
   撤销栈 1 条：「波纹删除 #2（3 条）」
撤销一次    ： #1[0,3000) #2[3000,8000) #3[8000,10000) #4[10000,11500)
```

宏命令的 `undo()` 一定要**倒着**撤销。看一个顺序要紧的例子：宏里先「剪切 #2，后半段叫 #4」，再「把 #4 移到 12 s」。正着撤，先撤剪切，#4 被删掉了，再撤移动时就找不到 #4。每条命令记下的旧值，都是在它前面那些命令执行完之后的状态，只有倒着撤，每条命令撤销时面对的状态才和它执行完时一样。

还有一个宏命令要考虑的问题：**执行到一半失败了怎么办**（比如第 3 条命令发现片段已经被锁定）。做法和数据库事务一样：把已经执行的那几条倒着撤销掉，整个宏就像没执行过。

### 3.2 命令合并：一次拖动只算一步

拖动片段时，每个 mousemove 事件都产生一条 `MoveClip`。上面的程序里，一次拖动产生了 120 条命令，不合并就要按 120 次 Ctrl+Z。

合并的做法：`Command` 加一个虚函数 `mergeWith(next)`，`History::run()` 执行完新命令后，先问栈顶能不能把它并进去。`MoveClip` 的规则是：同一个片段、同一次拖动（按下鼠标到松开，用 `gesture` 编号区分），就把自己的终点改成新命令的终点，**起点 `from_` 保留自己的**。这样撤销一次就回到拖动之前。

Qt 的 `QUndoCommand` 就是这么设计的：子类重写 `id()` 和 `mergeWith()`，`QUndoStack::push()` 发现新命令和栈顶的 `id()` 相同，就调用 `mergeWith()`；宏命令则用 `QUndoStack::beginMacro()` / `endMacro()` 把中间 push 的命令包成一条。用 Qt 写的开源剪辑软件 Shotcut，时间线上的移动、修剪、加滤镜就是一个个 `QUndoCommand` 子类。

文本编辑器也一样：连续打的一串字合成一条「输入」，遇到空格、换行或者光标跳走才断开。合并规则是产品决策，不是技术问题——合得太狠，一次撤销丢太多；合得太少，按 Ctrl+Z 按到手酸。

### 3.3 命令队列：把转码放到后台去做

命令是对象，所以可以**先存起来，以后再执行，甚至在另一个线程里执行**。导出视频要转码好几分钟，界面线程只负责把「转码第 k 段」这个命令放进队列，后台线程一个个取出来执行：

<figure class="diagram">
<svg viewBox="0 0 740 212" role="img" aria-label="命令队列：界面线程把转码任务（Job）提交进 pending 队列后立即返回；队列里排着 #5 到 #8，其中 #8 已被取消；两个后台线程各自从队列取任务，正在执行 #3 和 #4"><g style="font-family:var(--mono)"><rect x="20" y="70" width="120" height="56" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="80.0" y="96.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px;font-weight:700">界面线程</text><text x="80.0" y="111.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">submit() 立即返回</text><text x="20.0" y="40.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">生产者</text><line x1="140.0" y1="98.0" x2="187.0" y2="98.0" style="stroke:var(--link);stroke-width:1.4"/><path d="M196.0,98.0 L187.0,93.0 L187.0,103.0 z" style="fill:var(--link);stroke:var(--link);stroke-width:1.2"/><text x="168.0" y="88.0" text-anchor="middle" style="fill:var(--link);font-size:10.5px">Job</text><rect x="200" y="66" width="300" height="64" rx="5" style="fill:none;stroke:var(--border-2);stroke-width:1.4"/><text x="200.0" y="56.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">pending_（deque + mutex + 条件变量）</text><rect x="210" y="76" width="62" height="44" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="241.0" y="96.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">#5</text><text x="241.0" y="111.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">转码</text><rect x="282" y="76" width="62" height="44" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="313.0" y="96.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">#6</text><text x="313.0" y="111.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">转码</text><rect x="354" y="76" width="62" height="44" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="385.0" y="96.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">#7</text><text x="385.0" y="111.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">转码</text><rect x="426" y="76" width="62" height="44" rx="5" style="fill:var(--panel);stroke:var(--danger);stroke-width:1.4;stroke-dasharray:4 3"/><text x="457.0" y="96.0" text-anchor="middle" style="fill:var(--danger);font-size:11.5px">#8</text><text x="457.0" y="111.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">已取消</text><text x="350.0" y="152.0" text-anchor="middle" style="fill:var(--danger);font-size:10.5px">cancel(id)：还在排队的直接从队列里删掉</text><line x1="500.0" y1="98.0" x2="558.5" y2="59.0" style="stroke:var(--accent);stroke-width:1.4"/><path d="M566.0,54.0 L555.7,54.8 L561.3,63.2 z" style="fill:var(--accent);stroke:var(--accent);stroke-width:1.2"/><rect x="570" y="30" width="150" height="48" rx="5" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.4"/><text x="645.0" y="52.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px;font-weight:700">后台线程 0</text><text x="645.0" y="67.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">正在执行 #3</text><line x1="500.0" y1="98.0" x2="558.6" y2="138.9" style="stroke:var(--accent);stroke-width:1.4"/><path d="M566.0,144.0 L561.5,134.8 L555.8,143.0 z" style="fill:var(--accent);stroke:var(--accent);stroke-width:1.2"/><rect x="570" y="120" width="150" height="48" rx="5" style="fill:var(--panel);stroke:var(--accent);stroke-width:1.4"/><text x="645.0" y="142.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px;font-weight:700">后台线程 1</text><text x="645.0" y="157.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">正在执行 #4</text><text x="570.0" y="196.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">job.run() 在锁外执行</text><text x="20.0" y="196.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">实测：提交一个任务不到 1 µs，执行一个 30 ms</text></g></svg>
</figure>

下面的程序用了线程，只在 dp-lab 里跑（文章里的代码块在浏览器里运行，不能用线程）：

<!-- run: off -->
```cpp
// 《命令模式》：命令队列——界面线程只把转码任务放进队列就返回，后台线程逐个取出来执行；还没开始的任务可以取消
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;

struct Job {
    int id;
    std::string name;
    std::function<void()> run;           // 命令：要做的事
};

class JobQueue {
public:
    explicit JobQueue(int workers) {
        for (int i = 0; i < workers; ++i)
            threads_.emplace_back([this, i] { loop(i); });
    }
    ~JobQueue() {
        {
            std::lock_guard<std::mutex> lk(m_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : threads_) t.join();
    }
    int submit(std::string name, std::function<void()> run) {
        std::lock_guard<std::mutex> lk(m_);
        int id = ++nextId_;
        pending_.push_back({id, std::move(name), std::move(run)});
        cv_.notify_one();
        return id;
    }
    bool cancel(int id) {                // 只能取消还在排队的
        std::lock_guard<std::mutex> lk(m_);
        auto it = std::find_if(pending_.begin(), pending_.end(), [&](const Job& j) { return j.id == id; });
        if (it == pending_.end()) return false;
        pending_.erase(it);
        idle_.notify_all();
        return true;
    }
    void waitIdle() {
        std::unique_lock<std::mutex> lk(m_);
        idle_.wait(lk, [&] { return pending_.empty() && running_ == 0; });
    }
    std::vector<std::string> log() {
        std::lock_guard<std::mutex> lk(m_);
        return log_;
    }

private:
    void loop(int worker) {
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [&] { return stop_ || !pending_.empty(); });
                if (stop_ && pending_.empty()) return;
                job = std::move(pending_.front());
                pending_.pop_front();
                ++running_;
            }
            job.run();                   // 在锁外执行命令
            {
                std::lock_guard<std::mutex> lk(m_);
                --running_;
                log_.push_back("线程" + std::to_string(worker) + " 完成 #" + std::to_string(job.id));
            }
            idle_.notify_all();
        }
    }
    std::mutex m_;
    std::condition_variable cv_, idle_;
    std::deque<Job> pending_;
    std::vector<std::thread> threads_;
    std::vector<std::string> log_;
    int nextId_ = 0, running_ = 0;
    bool stop_ = false;
};

// 模拟转码一段视频：占满一个 CPU 核 30ms
void fakeTranscode() {
    auto end = Clock::now() + std::chrono::milliseconds(30);
    volatile unsigned x = 0;
    while (Clock::now() < end) x = x * 1664525u + 1013904223u;
}

int main() {
    for (int workers : {1, 2}) {
        JobQueue q(workers);
        std::vector<double> submitNs;
        std::vector<int> ids;
        auto t0 = Clock::now();
        for (int k = 1; k <= 8; ++k) {
            auto s = Clock::now();
            ids.push_back(q.submit("转码 bbb-10s.mp4 第 " + std::to_string(k) + " 段 → 720p", fakeTranscode));
            submitNs.push_back(std::chrono::duration<double, std::nano>(Clock::now() - s).count());
        }
        bool cancelled = q.cancel(ids.back());   // 用户点了最后一个任务的「取消」
        q.waitIdle();
        double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        std::sort(submitNs.begin(), submitNs.end());
        std::printf("%d 个后台线程：提交一个任务 %.0f ns（中位数），取消 #8 %s，7 个任务共 %.0f ms\n", workers,
                    submitNs[submitNs.size() / 2], cancelled ? "成功" : "失败（已经开始）", ms);
        std::printf("   ");
        for (auto& s : q.log()) std::printf("%s；", s.c_str());
        std::printf("\n");
    }
}
```

```text
1 个后台线程：提交一个任务 667 ns（中位数），取消 #8 成功，7 个任务共 210 ms
   线程0 完成 #1；线程0 完成 #2；线程0 完成 #3；线程0 完成 #4；线程0 完成 #5；线程0 完成 #6；线程0 完成 #7；
2 个后台线程：提交一个任务 125 ns（中位数），取消 #8 成功，7 个任务共 120 ms
   线程0 完成 #1；线程1 完成 #2；线程1 完成 #3；线程0 完成 #4；线程0 完成 #5；线程1 完成 #6；线程1 完成 #7；
```

界面线程提交一个任务不到 1 µs（GCC 上 84～125 ns），而任务本身要 30 ms；2 个后台线程时 7 个任务从 210 ms 降到 120 ms（两边一样）。几个要点：

- 这里的命令就是 `std::function<void()>`，不需要撤销，也就不需要 `Command` 类层次。线程池、`std::async`、GUI 框架的「投递到主线程执行」（Qt 的 `QMetaObject::invokeMethod`、各种 `runOnUiThread`）都是这个形状。
- **命令在锁外执行**。如果拿着队列的锁跑 30 ms 的转码，别的线程连提交都提交不了。
- **取消只能取消还在排队的**。已经开始的，要靠命令自己定期检查一个「取消标志」（`std::atomic<bool>` 或 C++20 的 `std::stop_token`）。
- **lambda 捕获的东西要活得比命令久**。命令可能在提交它的函数返回很久之后才执行，按引用捕获了局部变量就会悬空（第 4.2 节）。往队列里放的 lambda，一律按值捕获。

### 3.4 序列化：操作日志和崩溃恢复

命令的参数都是简单的值（id、时间、音量、滤镜字符串），可以写成一行文本。剪辑器把每条命令（还有 undo、redo）追加到一个日志文件里，崩溃后从上次保存的工程文件开始，把日志重放一遍，就恢复到崩溃前的状态：

<figure class="diagram">
<svg viewBox="0 0 800 180" role="img" aria-label="上面一行：最左边是上次保存的工程文件，后面跟着五行操作日志：move、vol、filter、split、undo，最右边是崩溃。下面一条箭头：恢复时先读工程文件，再按顺序重放日志"><g style="font-family:var(--mono)"><text x="20.0" y="22.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">工程文件（检查点）</text><rect x="20" y="32" width="130" height="46" rx="5" style="fill:var(--panel);stroke:var(--link);stroke-width:1.4"/><text x="85.0" y="53.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px;font-weight:700">project.json</text><text x="85.0" y="68.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">上次保存的完整状态</text><text x="180.0" y="22.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">操作日志：保存之后的每条命令，一行一条，追加写</text><rect x="180" y="32" width="106" height="46" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="233.0" y="53.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">move 3 9000</text><text x="233.0" y="68.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">第 1 行</text><rect x="292" y="32" width="106" height="46" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="345.0" y="53.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">vol 2 0.3…</text><text x="345.0" y="68.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">第 2 行</text><rect x="404" y="32" width="106" height="46" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="457.0" y="53.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">filter 1 fade</text><text x="457.0" y="68.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">第 3 行</text><rect x="516" y="32" width="106" height="46" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="569.0" y="53.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">split 2 5000 4</text><text x="569.0" y="68.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">第 4 行</text><rect x="628" y="32" width="106" height="46" rx="5" style="fill:var(--panel);stroke:var(--border-2);stroke-width:1.4"/><text x="681.0" y="53.0" text-anchor="middle" style="fill:var(--fg);font-size:11.5px">undo</text><text x="681.0" y="68.0" text-anchor="middle" style="fill:var(--muted);font-size:10px">第 5 行</text><text x="768.0" y="62.0" text-anchor="middle" style="fill:var(--danger);font-size:20px;font-weight:700">✕</text><text x="768.0" y="92.0" text-anchor="middle" style="fill:var(--danger);font-size:11px">崩溃</text><text x="20.0" y="124.0" text-anchor="start" style="fill:var(--fg);font-size:11px">恢复：读检查点 → 用 parse() 把日志一行行还原成命令 → 依次 execute（遇到 undo / redo 就调 History）</text><line x1="85.0" y1="80.0" x2="85.0" y2="131.0" style="stroke:var(--accent);stroke-width:1.4"/><path d="M85.0,140.0 L90.0,131.0 L80.0,131.0 z" style="fill:var(--accent);stroke:var(--accent);stroke-width:1.2"/><line x1="85.0" y1="140.0" x2="741.0" y2="140.0" style="stroke:var(--accent);stroke-width:1.4"/><path d="M750.0,140.0 L741.0,135.0 L741.0,145.0 z" style="fill:var(--accent);stroke:var(--accent);stroke-width:1.2"/><text x="20.0" y="166.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">保存工程时：写新的检查点，清空日志。日志只需要覆盖「上次保存到崩溃」这一段</text></g></svg>
</figure>

```cpp
// 《命令模式》：命令能写成一行文本——把每条命令（以及撤销、重做）追加到操作日志里，崩溃后从日志重放出同样的时间线
#include <chrono>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

struct Clip {
    int id, start, in, out;
    double volume = 1.0;
    std::vector<std::string> filters;
    bool operator==(const Clip& o) const {
        return id == o.id && start == o.start && in == o.in && out == o.out && volume == o.volume && filters == o.filters;
    }
};
struct Timeline {
    std::vector<Clip> clips;
    int indexOf(int id) const {
        for (int i = 0; i < (int)clips.size(); ++i)
            if (clips[i].id == id) return i;
        return -1;
    }
    Clip* find(int id) { return &clips[indexOf(id)]; }
};

class Command {
public:
    virtual ~Command() = default;
    virtual void execute(Timeline&) = 0;
    virtual void undo(Timeline&) = 0;
    virtual std::string serialize() const = 0;   // 一行文本，parse() 能还原
};
class MoveClip : public Command {
    int id_, to_, from_ = 0;
public:
    MoveClip(int id, int to) : id_(id), to_(to) {}
    void execute(Timeline& tl) override { from_ = tl.find(id_)->start; tl.find(id_)->start = to_; }
    void undo(Timeline& tl) override { tl.find(id_)->start = from_; }
    std::string serialize() const override { return "move " + std::to_string(id_) + " " + std::to_string(to_); }
};
class SetVolume : public Command {
    int id_; double v_, old_ = 0;
public:
    SetVolume(int id, double v) : id_(id), v_(v) {}
    void execute(Timeline& tl) override { old_ = tl.find(id_)->volume; tl.find(id_)->volume = v_; }
    void undo(Timeline& tl) override { tl.find(id_)->volume = old_; }
    std::string serialize() const override {
        char buf[64];
        std::snprintf(buf, sizeof buf, "vol %d %.17g", id_, v_);   // %.17g：double 写出去再读回来一位不差
        return buf;
    }
};
class AddFilter : public Command {
    int id_; std::string f_;
public:
    AddFilter(int id, std::string f) : id_(id), f_(std::move(f)) {}
    void execute(Timeline& tl) override { tl.find(id_)->filters.push_back(f_); }
    void undo(Timeline& tl) override { tl.find(id_)->filters.pop_back(); }
    std::string serialize() const override { return "filter " + std::to_string(id_) + " " + f_; }
};
class SplitClip : public Command {
    int id_, at_, newId_;
public:
    SplitClip(int id, int at, int newId) : id_(id), at_(at), newId_(newId) {}
    void execute(Timeline& tl) override {
        int i = tl.indexOf(id_);
        Clip right = tl.clips[i];
        int cut = right.in + (at_ - right.start);
        right.id = newId_; right.start = at_; right.in = cut;
        tl.clips[i].out = cut;
        tl.clips.insert(tl.clips.begin() + i + 1, right);
    }
    void undo(Timeline& tl) override {
        int r = tl.indexOf(newId_);
        tl.find(id_)->out = tl.clips[r].out;
        tl.clips.erase(tl.clips.begin() + r);
    }
    std::string serialize() const override {
        return "split " + std::to_string(id_) + " " + std::to_string(at_) + " " + std::to_string(newId_);
    }
};

// 反序列化：读一行，按第一个词造出对应的命令
std::unique_ptr<Command> parse(const std::string& line) {
    std::istringstream in(line);
    std::string op;
    int id = 0;
    in >> op >> id;
    if (op == "move")  { int to; in >> to; return std::make_unique<MoveClip>(id, to); }
    if (op == "vol")   { double v; in >> v; return std::make_unique<SetVolume>(id, v); }
    if (op == "split") { int at, nid; in >> at >> nid; return std::make_unique<SplitClip>(id, at, nid); }
    if (op == "filter") { std::string f; in >> std::ws; std::getline(in, f); return std::make_unique<AddFilter>(id, f); }
    return nullptr;
}

class History {
public:
    History(Timeline& tl, std::string* journal) : tl_(tl), journal_(journal) {}
    void run(std::unique_ptr<Command> c) {
        c->execute(tl_);
        append(c->serialize());              // 先改内存里的时间线，再追加一行日志
        done_.push_back(std::move(c));
        undone_.clear();
    }
    void undo() {
        if (done_.empty()) return;
        done_.back()->undo(tl_);
        append("undo");
        undone_.push_back(std::move(done_.back()));
        done_.pop_back();
    }
    void redo() {
        if (undone_.empty()) return;
        undone_.back()->execute(tl_);
        append("redo");
        done_.push_back(std::move(undone_.back()));
        undone_.pop_back();
    }
    // 崩溃恢复：从初始状态开始，把日志一行行重放
    void replay(const std::string& log) {
        std::istringstream in(log);
        std::string line;
        auto* saved = journal_;
        journal_ = nullptr;                  // 重放时不再写日志
        while (std::getline(in, line)) {
            if (line == "undo") undo();
            else if (line == "redo") redo();
            else run(parse(line));
        }
        journal_ = saved;
    }

private:
    void append(const std::string& s) { if (journal_) *journal_ += s + "\n"; }
    Timeline& tl_;
    std::string* journal_;
    std::vector<std::unique_ptr<Command>> done_, undone_;
};

Timeline initial() {
    return {{{1, 0, 0, 3000, 1.0, {} }, {2, 3000, 3000, 8000, 1.0, {} }, {3, 8000, 8000, 10000, 1.0, {} }}};
}

int main() {
    std::string journal;                     // 真实的剪辑器里是磁盘上的文件，每写一行 fflush + fsync
    Timeline tl = initial();
    {
        History h(tl, &journal);
        h.run(std::make_unique<MoveClip>(3, 9000));
        h.run(std::make_unique<SetVolume>(2, 0.1 * 3));
        h.run(std::make_unique<AddFilter>(1, "fade=t=in:d=1"));
        h.run(std::make_unique<SplitClip>(2, 5000, 4));
        h.undo();
        h.undo();
        h.redo();
        h.run(std::make_unique<AddFilter>(2, "eq=brightness=0.06:saturation=1.2"));
    }                                        // 「崩溃」：History 和撤销栈都没了，只剩 tl 用来对照
    std::printf("操作日志（%zu 字节）：\n%s", journal.size(), journal.c_str());

    Timeline recovered = initial();
    History h2(recovered, nullptr);
    h2.replay(journal);
    std::printf("重放日志后的时间线和崩溃前%s\n", recovered.clips == tl.clips ? "完全一致" : "不一致！");

    // 1000 次操作的日志有多大，重放要多久
    std::string big;
    Timeline t3 = initial();
    {
        History h(t3, &big);
        for (int k = 0; k < 1000; ++k) {
            int id = k % 3 + 1;
            if (k % 4 == 3) h.run(std::make_unique<AddFilter>(id, "eq=brightness=0.06:saturation=1.2"));
            else if (k % 2) h.run(std::make_unique<SetVolume>(id, 0.1 * (k % 7 + 1)));
            else h.run(std::make_unique<MoveClip>(id, t3.find(id)->start + 10));
        }
    }
    Timeline t4 = initial();
    History h4(t4, nullptr);
    auto s = std::chrono::steady_clock::now();
    h4.replay(big);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s).count();
    std::printf("1000 条命令的日志 %zu 字节（平均每条 %.1f 字节），重放 %.2f ms，结果%s\n", big.size(),
                big.size() / 1000.0, ms, t4.clips == t3.clips ? "一致" : "不一致！");
}
```

```text
操作日志（134 字节）：
move 3 9000
vol 2 0.30000000000000004
filter 1 fade=t=in:d=1
split 2 5000 4
undo
undo
redo
filter 2 eq=brightness=0.06:saturation=1.2
重放日志后的时间线和崩溃前完全一致
1000 条命令的日志 22582 字节（平均每条 22.6 字节），重放 1.14 ms，结果一致
```

- 1000 条命令的日志只有 22582 字节，平均每条 22.6 字节；重放 1000 条 Clang 1.14 ms、GCC 0.19 ms（差别主要在 libc++ 构造 `istringstream` 比较慢）。
- 注意第二行的 `0.30000000000000004`：`0.1 * 3` 在 double 里并不是 0.3。如果用 `%.2f` 写日志，重放出来的是 0.3，和崩溃前差了一个最低位，「完全一致」就不成立了。浮点数写进文本要用 `%.17g`（或 C++17 的 `std::to_chars`），保证读回来一位不差。
- 反序列化要一个「按名字造对象」的地方（`parse()`），这就是《简单工厂与工厂方法》里的工厂。
- **重放要求命令是确定的**：同样的初始状态 + 同样的命令序列 = 同样的结果。命令里不能读当前时间、随机数、全局计数器（这又是 `SplitClip` 的新 id 要在构造时定好的原因：日志里写着 `split 2 5000 4`，重放时还是 4）。
- 这个思路在别处也很常见：数据库的预写日志（WAL）先写日志再改数据；Redis 的 AOF 持久化把每条写命令追加到文件里，重启时重放；工程文件（检查点）+ 日志，和 Redis 的 RDB 快照 + AOF 是同一个结构。

FFmpeg 里也有现成的「命令」：滤镜可以在运行时接收命令（`avfilter_graph_send_command()`），`sendcmd` / `asendcmd` 滤镜按时间点把写成文本的命令发给别的滤镜。比如在第 5 秒把音量降到 0.1：

```bash
ffmpeg -i bbb-10s.mp4 -vn -af "asendcmd=c='5.0 volume volume 0.1',volume=1.0,astats=metadata=1:reset=1,ametadata=print:key=lavfi.astats.Overall.RMS_level:file=-" -f null -
```

实测 `bbb-10s.mp4` 的音频：前 5 秒平均 RMS 电平 -32.6 dB；后 5 秒，带这条命令是 -52.1 dB，不带是 -32.1 dB——正好低了 20 dB，也就是幅度乘 0.1。命令写成文本 `5.0 volume volume 0.1`（什么时间、发给哪个滤镜、什么命令、什么参数），接收者 `volume` 滤镜在自己的 `process_command` 回调里执行。

## 4. 现代 C++ 写法

### 4.1 一对 lambda、`std::variant`

每种操作写一个类，样板代码很多。C++ 里还有两种常见的写法：

1. **一对 `std::function`**：`execute` 和 `undo` 各是一个 lambda，旧值在创建命令时捕获。不用定义任何类，适合操作种类多、每种都很简单的场景。开源剪辑软件 Kdenlive 的时间线模型就是这么写的：每个操作产生一对 `std::function<bool()>`（undo、redo），几个小操作的 lambda 可以串成一对大的，最后包成一条 `QUndoCommand` 放进 Qt 的撤销栈。
2. **`std::variant`**：每种命令是一个普通的结构体，命令就是 `variant<Move, Volume, ...>`，用 `std::visit` 分派。命令是值，可以直接放进 `vector`，不用堆分配，复制、比较、序列化都方便。

```cpp
// 《命令模式》：三种写法的命令——虚函数类、一对 std::function（lambda）、std::variant——各占多少内存
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <new>
#include <string>
#include <variant>
#include <vector>

static std::size_t g_live = 0, g_allocs = 0;
void* operator new(std::size_t n) {
    auto* p = static_cast<std::size_t*>(std::malloc(n + 16));
    if (!p) std::abort();
    *p = n;
    g_live += n;
    ++g_allocs;
    return reinterpret_cast<char*>(p) + 16;
}
void operator delete(void* p) noexcept {
    if (!p) return;
    auto* h = reinterpret_cast<std::size_t*>(static_cast<char*>(p) - 16);
    g_live -= *h;
    std::free(h);
}
void operator delete(void* p, std::size_t) noexcept { operator delete(p); }

struct Clip { int id; int start; double volume; };
struct Timeline {
    std::vector<Clip> clips;
    Clip& find(int id) {
        for (auto& c : clips)
            if (c.id == id) return c;
        return clips.front();
    }
};

// ---- 写法 1：虚函数类（前面一直在用的）----
struct Command {
    virtual ~Command() = default;
    virtual void execute(Timeline&) = 0;
    virtual void undo(Timeline&) = 0;
};
struct SetVolume : Command {
    int id; double v, old = 0;
    SetVolume(int i, double x) : id(i), v(x) {}
    void execute(Timeline& tl) override { auto& c = tl.find(id); old = c.volume; c.volume = v; }
    void undo(Timeline& tl) override { tl.find(id).volume = old; }
};

// ---- 写法 2：一对 lambda ----
struct Action {
    std::function<void()> execute, undo;
};
Action setVolume(Timeline& tl, int id, double v) {
    double old = tl.find(id).volume;               // 构造时就把旧值记下
    // 捕获 id 而不是 Clip&：vector 扩容后引用会悬空（见 dangling_capture.cpp）
    return {[&tl, id, v] { tl.find(id).volume = v; },
            [&tl, id, old] { tl.find(id).volume = old; }};
}

// ---- 写法 3：std::variant，命令是普通的值 ----
struct Move { int id, to, from; };
struct Volume { int id; double v, old; };
using Cmd = std::variant<Move, Volume>;

template <class... F> struct overloaded : F... { using F::operator()...; };
template <class... F> overloaded(F...) -> overloaded<F...>;

void execute(Timeline& tl, Cmd& c) {
    std::visit(overloaded{
        [&](Move& m) { auto& x = tl.find(m.id); m.from = x.start; x.start = m.to; },
        [&](Volume& v) { auto& x = tl.find(v.id); v.old = x.volume; x.volume = v.v; }}, c);
}
void undo(Timeline& tl, const Cmd& c) {
    std::visit(overloaded{
        [&](const Move& m) { tl.find(m.id).start = m.from; },
        [&](const Volume& v) { tl.find(v.id).volume = v.old; }}, c);
}

template <class F>
void measure(const char* name, std::size_t elem, F build) {
    std::size_t live0 = g_live, a0 = g_allocs;
    std::size_t n = build();
    std::size_t heap = (g_live - live0) / n;
    std::printf("%-28s 数组元素 %2zu + 堆上 %2zu = %2zu 字节，%.0f 次堆分配\n", name, elem, heap, elem + heap,
                double(g_allocs - a0) / n);
}

int main() {
    Timeline tl{{{1, 0, 1.0}, {2, 3000, 1.0}, {3, 8000, 1.0}}};
    constexpr int N = 1000;

    std::vector<std::unique_ptr<Command>> h1;
    std::vector<Action> h2;
    std::vector<Cmd> h3;
    h1.reserve(N); h2.reserve(N); h3.reserve(N);   // 先把三个历史的数组分配好，只量命令本身
    std::printf("每条命令占的内存（历史数组已经预先 reserve）：\n");
    measure("unique_ptr<Command>", sizeof(std::unique_ptr<Command>), [&] {
        for (int i = 0; i < N; ++i) { h1.push_back(std::make_unique<SetVolume>(2, 0.5)); h1.back()->execute(tl); }
        return h1.size();
    });
    measure("Action（两个 std::function）", sizeof(Action), [&] {
        for (int i = 0; i < N; ++i) { h2.push_back(setVolume(tl, 2, 0.5)); h2.back().execute(); }
        return h2.size();
    });
    measure("variant<Move, Volume>", sizeof(Cmd), [&] {
        for (int i = 0; i < N; ++i) { h3.push_back(Volume{2, 0.5, 0}); execute(tl, h3.back()); }
        return h3.size();
    });

    // 三种都能撤销
    tl.find(2).volume = 1.0;
    h3.clear();
    h3.push_back(Move{3, 9000, 0});
    h3.push_back(Volume{2, 0.3, 0});
    for (auto& c : h3) execute(tl, c);
    std::printf("variant 版执行后：#3 在 %dms，#2 音量 %.1f\n", tl.find(3).start, tl.find(2).volume);
    for (auto it = h3.rbegin(); it != h3.rend(); ++it) undo(tl, *it);
    std::printf("倒着撤销后    ：#3 在 %dms，#2 音量 %.1f\n", tl.find(3).start, tl.find(2).volume);
}
```

```text
每条命令占的内存（历史数组已经预先 reserve）：
unique_ptr<Command>          数组元素  8 + 堆上 32 = 40 字节，1 次堆分配
Action（两个 std::function） 数组元素 64 + 堆上 64 = 128 字节，2 次堆分配
variant<Move, Volume>        数组元素 32 + 堆上  0 = 32 字节，0 次堆分配
variant 版执行后：#3 在 9000ms，#2 音量 0.3
倒着撤销后    ：#3 在 8000ms，#2 音量 1.0
```

两个 `std::function` 的那一种最占内存：每个 lambda 捕获了 `&tl`、`id`、`v` 共 24 字节，超过了 `std::function` 内部的小缓冲区（libstdc++ 是 16 字节；libc++ 要连同自己的虚表指针一起放，也放不下），每个 lambda 都要单独分配一次。GCC 上每条是 112 字节（堆上 48），Clang 上是 128 字节（堆上 64）。

`variant` 的代价在扩展性上：加一种命令要改 `variant` 的类型列表，以及每一处 `std::visit`，是一个**封闭的集合**；虚函数的写法是**开放的**，加一种命令只要新写一个类，`History` 不用动。这和《策略模式》里比较的是同一个取舍：类型集合固定、追求性能，用 `variant`；要让插件、脚本能加新命令，用虚函数。

### 4.2 按引用捕获：悬空的 lambda

lambda 写法最容易踩的坑：**捕获了引用，而被引用的东西比命令先死**。下面的命令捕获了 `vector` 里某个片段的引用，之后加了一个片段，`vector` 扩容搬到新内存，旧的片段被释放；撤销时写的是已经释放的内存：

<!-- run: off -->
```cpp
// 《命令模式》：lambda 命令按引用捕获了 vector 里的片段，加一个片段后 vector 扩容，撤销写进了已经释放的内存
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

struct Clip { int id; std::string source; double volume; };
struct Timeline {
    std::vector<Clip> clips;
    Clip* find(int id) {
        for (auto& c : clips)
            if (c.id == id) return &c;
        return nullptr;
    }
};
struct Action { std::function<void()> execute, undo; };

int main() {
    Timeline tl{{{1, "bbb-10s.mp4", 1.0}, {2, "bbb-10s.mp4", 1.0}, {3, "bbb-10s.mp4", 1.0}}};
    std::vector<Action> history;

    auto setVolume = [&](int id, double v) {
        Clip& c = *tl.find(id);               // 引用的是 vector 里的元素
        double old = c.volume;
        Action a{[&c, v] { c.volume = v; }, [&c, old] { c.volume = old; }};
        a.execute();
        history.push_back(std::move(a));
    };

    setVolume(2, 0.3);
    std::printf("调音量后：#2 音量 %.1f，clips 容量 %zu\n", tl.find(2)->volume, tl.clips.capacity());
    tl.clips.push_back({4, "bbb-10s.mp4", 1.0});   // 加一个片段：容量不够，vector 搬到新内存
    std::printf("加片段后：clips 容量 %zu，旧的 3 个片段已经被释放\n", tl.clips.capacity());
    history.back().undo();                    // c 指向旧内存
    std::printf("撤销后  ：#2 音量 %.1f（应该回到 1.0）\n", tl.find(2)->volume);
}
```

```text
$ clang++ -std=c++17 -O2 dangling_capture.cpp && ./a.out
调音量后：#2 音量 0.3，clips 容量 3
加片段后：clips 容量 6，旧的 3 个片段已经被释放
撤销后  ：#2 音量 0.3（应该回到 1.0）

$ clang++ -std=c++17 -O1 -g -fsanitize=address dangling_capture.cpp && ./a.out
==35374==ERROR: AddressSanitizer: heap-use-after-free on address 0x60c000000208 ...
    #0 ... in main::$_0::operator()(int, double) const::'lambda0'()::operator()() const dangling_capture.cpp:25
SUMMARY: AddressSanitizer: heap-use-after-free dangling_capture.cpp:25
```

不加 ASan 时程序不崩、退出码 0，只是撤销「没有效果」——这是最难查的一种 bug。GCC 上现象完全一样，ASan 同样报 heap-use-after-free。修法和第 2.2 节的道理一样：**捕获 id，执行时再去找**（`modern.cpp` 里的 `setVolume` 就是这么写的）。

按引用捕获还有几种常见的变体：

- 捕获了局部变量（`[&]`），命令放进队列，函数返回后才执行。
- 捕获了 `this`，命令还在撤销栈里，对象已经析构（比如关掉了一个时间线面板）。
- 捕获了 `shared_ptr`，没有悬空，但命令一直留在撤销栈里，对象就一直释放不掉——撤销历史成了内存泄漏的源头。撤销栈要有上限（练习 3）。

### 4.3 开销实测

100 万条命令（移动和调音量交替），每条「创建 + 执行 + 入栈」和「撤销 + 出栈」的耗时：

<figure class="diagram">
<svg viewBox="0 0 856 156" role="img" aria-label="每条命令的开销。执行加入栈：Clang 虚函数类 7.88 ns，两个 std::function 9.83，variant 1.51；GCC 4.27、9.33、1.29。撤销加出栈：Clang 5.73、6.24、1.15；GCC 4.62、5.85、0.74。每条占的内存：40、128（GCC 112）、32 字节"><g style="font-family:var(--mono)"><text x="158.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">执行+入栈 · Clang</text><rect x="158.0" y="30.0" width="64.5" height="20.0" rx="2" style="fill:var(--link)"/><text x="228.5" y="45.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">7.88 ns</text><rect x="158.0" y="60.0" width="80.4" height="20.0" rx="2" style="fill:var(--warn)"/><text x="244.4" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">9.83 ns</text><rect x="158.0" y="90.0" width="12.4" height="20.0" rx="2" style="fill:var(--accent)"/><text x="176.4" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">1.51 ns</text><text x="332.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">执行+入栈 · GCC</text><rect x="332.0" y="30.0" width="34.9" height="20.0" rx="2" style="fill:var(--link)"/><text x="372.9" y="45.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">4.27 ns</text><rect x="332.0" y="60.0" width="76.3" height="20.0" rx="2" style="fill:var(--warn)"/><text x="414.3" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">9.33 ns</text><rect x="332.0" y="90.0" width="10.6" height="20.0" rx="2" style="fill:var(--accent)"/><text x="348.6" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">1.29 ns</text><text x="506.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">撤销+出栈 · Clang</text><rect x="506.0" y="30.0" width="46.9" height="20.0" rx="2" style="fill:var(--link)"/><text x="558.9" y="45.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">5.73 ns</text><rect x="506.0" y="60.0" width="51.1" height="20.0" rx="2" style="fill:var(--warn)"/><text x="563.1" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">6.24 ns</text><rect x="506.0" y="90.0" width="9.4" height="20.0" rx="2" style="fill:var(--accent)"/><text x="521.4" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">1.15 ns</text><text x="680.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">撤销+出栈 · GCC</text><rect x="680.0" y="30.0" width="37.8" height="20.0" rx="2" style="fill:var(--link)"/><text x="723.8" y="45.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">4.62 ns</text><rect x="680.0" y="60.0" width="47.9" height="20.0" rx="2" style="fill:var(--warn)"/><text x="733.9" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">5.85 ns</text><rect x="680.0" y="90.0" width="6.1" height="20.0" rx="2" style="fill:var(--accent)"/><text x="692.1" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.74 ns</text><text x="150.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">unique_ptr&lt;Command&gt;</text><text x="150.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">两个 std::function</text><text x="150.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">variant + visit</text><text x="10.0" y="148.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench_forms：-O2，100 万条命令（移动和调音量交替），7 轮中位数</text></g></svg>
</figure>

- `variant` 最快（Clang 执行 1.51 ns、撤销 1.15 ns），因为没有堆分配，命令都连续放在一个数组里。
- 两个 `std::function` 最慢（Clang 9.83 ns），每条两次堆分配。
- 虚函数类在 GCC 上（4.27 ns）比 Clang 上（7.88 ns）快不少，两边的排序是一致的：`variant` 最快，两个 `std::function` 最慢。

不过这是纳秒级的差别。剪辑器里用户一秒最多做几十个操作，用哪种写法都不会成为瓶颈；选哪种主要看前面说的扩展性和内存，以及要不要序列化。

## 5. 命令和相似的模式

| | 命令 | 策略 | 备忘录 | 责任链 |
|---|---|---|---|---|
| 封装的是什么 | 一个请求（做什么 + 参数 + 怎么撤销） | 一种算法（怎么做） | 一个对象某一时刻的状态 | 处理请求的一串对象 |
| 典型的问题 | 撤销 / 重做、排队、记日志、宏 | 同一件事有几种做法，运行时选一种 | 恢复到之前的某个状态 | 请求该谁处理，事先不确定 |
| 对象的生命 | 创建、执行，然后进撤销栈或日志，可能活很久 | 通常长期持有，反复调用 | 存在历史里，恢复时用 | 链条长期存在，请求一次次流过 |
| 本文里的例子 | `MoveClip`、`SplitClip`、转码任务 | 缩放算法（上上篇） | 整份快照、`DeleteClip` 里存的 `saved_` | — |

- **命令和策略**：都是「把行为装进对象」，代码上可能一模一样（都是一个虚函数，或者一个 `std::function`）。区别在用途：策略是**怎么做**，同一件事的不同做法，调用者持有一个策略反复用；命令是**做什么**，每个命令对象代表一次具体的请求，带着参数，执行一次（或撤销一次）就完了。
- **命令和备忘录**：两种撤销思路。备忘录存**状态**，恢复时整个换回去（第 1.2 节的快照就是）；命令存**操作**，撤销时做反向的事。实际中经常一起用：`DeleteClip` 撤销时需要被删的整个片段，就在命令里存了一个小备忘录 `saved_`。改动小、能算出旧值的用命令；改动复杂、反向操作难写的（比如「自动调色」改了一大堆参数），就在命令里存一份被改部分的备忘录。
- **命令和责任链**：可以组合。比如菜单项发出一个「复制」命令，沿着「当前焦点控件 → 面板 → 主窗口」这条链传，谁能处理谁处理（Qt 的事件传递、macOS 的 responder chain）。命令是被传的东西，责任链是传的方式。
- **命令和组合**：宏命令就是组合模式用在命令上。

## 6. 小结

1. 命令模式把「要做的一件事」变成对象。有了对象，操作就能被存下来、撤销、重做、合并、排队、写进日志、在另一个线程执行。
2. 撤销的两种思路：存状态（快照、备忘录）和存操作（命令）。实测 200 个片段做 1000 次操作，整份快照占 34368 KB，共享快照 3344 KB，命令 47 KB。
3. 写可撤销的命令：`execute()` 里记旧值；撤销时恢复旧值，而不是做反向运算；命令里存 id，不存指针；新对象的 id 在构造时定好。撤销后做新命令要清空重做栈。
4. 宏命令倒着撤销；连续的同类操作（拖动、打字）用 `mergeWith` 合成一条。
5. 命令能序列化，就能做操作日志、崩溃恢复、录制回放。前提是命令是确定的，浮点数要按 `%.17g` 写。
6. 不需要撤销的命令，`std::function<void()>` 就够了（任务队列、按键绑定）。要撤销时，虚函数类可扩展，`variant` 快且省内存；lambda 只按值捕获 id 和数值，不要捕获引用。

## 7. 练习

1. **辨析**：下面四种情况，哪些用了命令模式？不是的话更像哪个模式？
   - (a) 播放器把「暂停」「seek 到 30 s」「切换字幕轨」做成对象，放进一个队列，播放线程在两帧之间取出来执行。
   - (b) `std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.start < b.start; })` 里的比较函数。
   - (c) 剪辑器每 5 分钟把整个工程序列化一份，提供「恢复到 10 分钟前」。
   - (d) 快捷键表 `std::unordered_map<std::string, std::function<void()>>`，用户可以在设置里把「剪切」改绑到别的键上。
2. **动手**：给第 3.1 节的 `macro_merge.cpp` 加一个修剪命令 `TrimClip`：拖动片段的右边缘改变长度（最短 100 ms），能撤销、重做；同一次拖动产生的修剪合并成一条。模拟两次拖动：第一次从 5000 ms 拖到 2000 ms，第二次从 2000 ms 拖到 2500 ms，撤销栈里应该是 2 条。
3. **设计**：撤销历史不能无限长（第 4.2 节说了，它还可能让对象释放不掉）。给 `History` 加两个功能：最多保留 N 步，超出时丢掉最老的；记住「保存点」，标题栏在有未保存的改动时显示 `*`——撤销回保存时的状态，`*` 要消失。想一想：保存后撤销一步、再做一条新命令，这时还能回到保存点吗？保存点被丢掉的那一步覆盖了呢？
4. **找问题**：下面的「音量 +x」命令，撤销后音量不对。哪里错了？怎么改？

   ```cpp
   // 《命令模式》练习 4（有 bug）：「音量 +x」命令执行时把音量限制在 0～2，撤销时减回 x，撤销后音量不对
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
       Clip clip;
       clip.volume = 1.5;
       AdjustVolume up(clip, 1.0);
       up.execute();
       std::printf("1.5 加 1.0：%.1f（限制在 2.0）\n", clip.volume);
       up.undo();
       std::printf("撤销：%.1f（应该是 1.5）\n", clip.volume);
   }
   ```

<details>
<summary>点击查看答案</summary>

**1.**

- (a) 是命令模式：请求被做成对象、排队、在另一个线程执行（第 3.3 节）。不需要撤销，也照样是命令。
- (b) 是策略模式：比较函数是「怎么比」，`sort` 在内部反复调用它，它不代表一次请求。
- (c) 是备忘录（快照）：存的是状态，不是操作。
- (d) 是命令模式，用的是它最早的用途：把调用者（按键）和请求（剪切）解耦，调用者可以在运行时换成别的命令，GoF 说的「用不同的请求把客户参数化」。这里不需要撤销，`std::function<void()>` 就够了。

**2.** 和 `MoveClip` 一样，在 `execute()` 里记旧长度，`mergeWith` 时保留第一条的旧长度，只更新新长度。注意截断到 100 ms 只影响新值，撤销用的是记下来的旧值：

```cpp
// 《命令模式》练习 2：加一个修剪命令 TrimClip（拖动片段右边缘改长度），能撤销，同一次拖动合并成一条
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Clip { int id, start, len; };
struct Timeline {
    std::vector<Clip> clips;
    Clip* find(int id) {
        for (auto& c : clips)
            if (c.id == id) return &c;
        return nullptr;
    }
};

class Command {
public:
    virtual ~Command() = default;
    virtual void execute(Timeline&) = 0;
    virtual void undo(Timeline&) = 0;
    virtual bool mergeWith(const Command&) { return false; }
};

class TrimClip : public Command {
public:
    TrimClip(int id, int len, int gesture) : id_(id), len_(len), gesture_(gesture) {}
    void execute(Timeline& tl) override {
        Clip* c = tl.find(id_);
        old_ = c->len;
        c->len = len_ < 100 ? 100 : len_;    // 最短 100ms；撤销用的是 old_，不受这里截断的影响
    }
    void undo(Timeline& tl) override { tl.find(id_)->len = old_; }
    bool mergeWith(const Command& next) override {
        auto* t = dynamic_cast<const TrimClip*>(&next);
        if (!t || t->id_ != id_ || t->gesture_ != gesture_) return false;
        len_ = t->len_;                       // 保留第一条的 old_
        return true;
    }

private:
    int id_, len_, gesture_, old_ = 0;
};

class History {
public:
    explicit History(Timeline& tl) : tl_(tl) {}
    void run(std::unique_ptr<Command> c) {
        c->execute(tl_);
        undone_.clear();
        if (!done_.empty() && done_.back()->mergeWith(*c)) return;
        done_.push_back(std::move(c));
    }
    void undo() {
        if (done_.empty()) return;
        done_.back()->undo(tl_);
        undone_.push_back(std::move(done_.back()));
        done_.pop_back();
    }
    void redo() {
        if (undone_.empty()) return;
        undone_.back()->execute(tl_);
        done_.push_back(std::move(undone_.back()));
        undone_.pop_back();
    }
    std::size_t size() const { return done_.size(); }

private:
    Timeline& tl_;
    std::vector<std::unique_ptr<Command>> done_, undone_;
};

int main() {
    Timeline tl{{{1, 0, 3000}, {2, 3000, 5000}}};
    History h(tl);
    for (int len = 4900; len >= 2000; len -= 100) h.run(std::make_unique<TrimClip>(2, len, 1));  // 第 1 次拖动
    for (int len = 2100; len <= 2500; len += 100) h.run(std::make_unique<TrimClip>(2, len, 2));  // 第 2 次拖动
    std::printf("两次拖动后 #2 长 %dms，撤销栈 %zu 条\n", tl.find(2)->len, h.size());
    h.undo();
    std::printf("撤销一次：%dms\n", tl.find(2)->len);
    h.undo();
    std::printf("再撤销  ：%dms\n", tl.find(2)->len);
    h.redo();
    std::printf("重做    ：%dms\n", tl.find(2)->len);
}
```

```text
两次拖动后 #2 长 2500ms，撤销栈 2 条
撤销一次：2000ms
再撤销  ：5000ms
重做    ：2000ms
```

**3.** 保存点记成「保存时撤销栈的长度」`clean_`，当前长度等于它就是没有改动。三种情况要处理：

- 撤销、重做只改变撤销栈的长度，`isClean()` 自然就对了。
- 做新命令时，如果 `clean_` 比当前长度大，说明保存点在重做栈里，而重做栈马上要被清空，保存点再也回不去了，记成 -1。
- 超过上限丢掉最老的一条时，所有位置都左移一格，`clean_` 也减 1；从 0 减到 -1，也是回不去了。

撤销栈要从前面删，换成 `deque`：

```cpp
// 《命令模式》练习 3：撤销历史最多保留 N 步，并记住「保存点」，标题栏据此显示有没有未保存的改动
#include <cstdio>
#include <deque>
#include <memory>
#include <vector>

struct Timeline { int volume = 10; };

class Command {
public:
    virtual ~Command() = default;
    virtual void execute(Timeline&) = 0;
    virtual void undo(Timeline&) = 0;
};
class SetVolume : public Command {
    int v_, old_ = 0;
public:
    explicit SetVolume(int v) : v_(v) {}
    void execute(Timeline& tl) override { old_ = tl.volume; tl.volume = v_; }
    void undo(Timeline& tl) override { tl.volume = old_; }
};

class History {
public:
    History(Timeline& tl, std::size_t limit) : tl_(tl), limit_(limit) {}
    void run(std::unique_ptr<Command> c) {
        c->execute(tl_);
        if (clean_ > (long)done_.size()) clean_ = -1;   // 保存点在被清掉的重做栈里：再也回不去了
        undone_.clear();
        done_.push_back(std::move(c));
        if (done_.size() > limit_) {                   // 超过上限，丢掉最老的一条
            done_.pop_front();
            if (clean_ >= 0) --clean_;                 // 保存点跟着左移；从 0 变成 -1 也是回不去了
        }
    }
    void undo() {
        if (done_.empty()) return;
        done_.back()->undo(tl_);
        undone_.push_back(std::move(done_.back()));
        done_.pop_back();
    }
    void redo() {
        if (undone_.empty()) return;
        undone_.back()->execute(tl_);
        done_.push_back(std::move(undone_.back()));
        undone_.pop_back();
    }
    void setClean() { clean_ = (long)done_.size(); }   // 保存文件时调用
    bool isClean() const { return clean_ == (long)done_.size(); }

private:
    Timeline& tl_;
    std::size_t limit_;
    std::deque<std::unique_ptr<Command>> done_;        // 要从前面删，用 deque
    std::vector<std::unique_ptr<Command>> undone_;
    long clean_ = 0;                                   // 保存点 = 那时撤销栈的长度；-1 表示回不去了
};

void title(const History& h, const Timeline& tl, const char* what) {
    std::printf("%s：音量 %d，标题栏「剪辑工程%s」\n", what, tl.volume, h.isClean() ? "" : " *");
}

int main() {
    Timeline tl;
    History h(tl, 3);
    title(h, tl, "打开文件");
    h.run(std::make_unique<SetVolume>(11));
    h.run(std::make_unique<SetVolume>(12));
    title(h, tl, "改两次");
    h.setClean();
    title(h, tl, "保存");
    h.undo();
    title(h, tl, "撤销一次");
    h.redo();
    title(h, tl, "重做，回到保存点");
    h.undo();
    h.run(std::make_unique<SetVolume>(20));
    title(h, tl, "撤销一次再改成 20");
    h.undo();
    title(h, tl, "再撤销一次（保存点所在的分支已经没了）");

    Timeline t2;
    History h2(t2, 3);
    h2.setClean();
    for (int v = 11; v <= 14; ++v) h2.run(std::make_unique<SetVolume>(v));
    h2.undo(); h2.undo(); h2.undo(); h2.undo();
    title(h2, t2, "\n新工程：保存后改 4 次，再撤销到底（只留 3 步）");
}
```

```text
打开文件：音量 10，标题栏「剪辑工程」
改两次：音量 12，标题栏「剪辑工程 *」
保存：音量 12，标题栏「剪辑工程」
撤销一次：音量 11，标题栏「剪辑工程 *」
重做，回到保存点：音量 12，标题栏「剪辑工程」
撤销一次再改成 20：音量 20，标题栏「剪辑工程 *」
再撤销一次（保存点所在的分支已经没了）：音量 11，标题栏「剪辑工程 *」

新工程：保存后改 4 次，再撤销到底（只留 3 步）：音量 11，标题栏「剪辑工程 *」
```

Qt 的 `QUndoStack` 也是这么做的：`setClean()` 记下当前位置，`setUndoLimit()` 限制步数，保存点被丢掉后 `isClean()` 永远是 false。

**4.** `undo()` 用了反向运算 `volume -= delta`，可是 `execute()` 里做了截断：1.5 + 1.0 被限制到 2.0，实际只加了 0.5，撤销却减了 1.0，得到 1.0。凡是执行时有截断、取整、「已经存在就不加」这类非线性的操作，反向运算都会错；浮点数加了再减也不一定回到原值。**撤销时恢复旧值，不要做反向运算**：

```cpp
// 《命令模式》练习 4（修好）：执行时记下旧值，撤销时直接恢复旧值，不做反向运算
#include <algorithm>
#include <cstdio>

struct Clip { double volume = 1.0; };

class AdjustVolume {
public:
    AdjustVolume(Clip& c, double delta) : c_(c), delta_(delta) {}
    void execute() {
        old_ = c_.volume;
        c_.volume = std::clamp(c_.volume + delta_, 0.0, 2.0);
    }
    void undo() { c_.volume = old_; }

private:
    Clip& c_;
    double delta_, old_ = 0;
};

int main() {
    Clip clip;
    clip.volume = 1.5;
    AdjustVolume up(clip, 1.0);
    up.execute();
    std::printf("1.5 加 1.0：%.1f（限制在 2.0）\n", clip.volume);
    up.undo();
    std::printf("撤销：%.1f\n", clip.volume);
    for (int i = 0; i < 10; ++i) { up.execute(); up.undo(); }
    std::printf("再执行、撤销 10 个来回：%.17g\n", clip.volume);
}
```

```text
1.5 加 1.0：2.0（限制在 2.0）
撤销：1.5
再执行、撤销 10 个来回：1.5
```

还有一个隐患：这个命令持有 `Clip&`，和第 4.2 节一样，片段所在的 `vector` 一扩容就悬空了。放进撤销栈的命令，最好也改成存 id。

</details>
