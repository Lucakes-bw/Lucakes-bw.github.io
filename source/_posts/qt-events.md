---
title: 事件的分发与传播
date: 2026-10-28 13:00:00
updated: 2026-10-28 13:00:00
categories:
  - 客户端开发
  - 事件与多线程
tags:
  - Qt
  - C++
  - 事件循环
order: 1
description: 一次鼠标点击从事件循环出发，依次经过 QApplication::notify、应用程序的事件过滤器、按钮的事件过滤器、event()、mousePressEvent()，最后才变成 pressed 信号；任何一个环节返回 true，后面的环节就都收不到了。标签忽略了点击，事件会传给它的父控件，位置也换算成了父控件的坐标；父控件也忽略，再往上传，直到有控件接受。sendEvent 立即处理，postEvent 放进队列，回到事件循环才处理。在界面线程中做一件耗时 1 秒的事，每 50 毫秒一次的心跳停了 1002 毫秒；在耗时的循环中调用 processEvents 能让界面不卡，可是用户第二次点击时，同一个任务被嵌套着又执行了一遍，本该 1 秒结束的任务 1 用了 2 秒，而且必须等任务 2 先结束。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解一个事件从事件循环到控件的完整路径：`notify`、事件过滤器、`event()`、具体的事件处理函数，知道在哪一步可以拦截；理解鼠标、键盘事件的 accept / ignore 和向父控件的传播；会用 `sendEvent`、`postEvent` 发送事件，会定义自己的事件类型；理解为什么不能在界面线程中做耗时的事，以及 `processEvents` 为什么不是好的解决办法。
>
> **前置知识**：第一章《环境与第一个 Qt 程序》的事件循环，第二章第 2.1 篇的嵌套事件循环、第 2.4 篇的事件处理函数。
>
> **实验环境**：macOS 27.0（Apple M6），Qt 6.12.0，Release 构建。

## 1. 事件经过的路径

前两章已经多次用到事件：第一章用事件过滤器打印了按钮收到的所有事件，第二章的开关控件重写了 `paintEvent` 和 `mouseReleaseEvent`。这一篇把它们串起来：一个事件从事件循环中取出来以后，到底经过了哪些环节？

### 1.1 一次点击的五站

在每一个可以插手的地方都打印一行，然后向按钮发送一次鼠标按下：

```cpp
// 第 1 站：重写 QApplication::notify，所有事件都要经过它
class App : public QApplication
{
public:
    using QApplication::QApplication;
    bool notify(QObject *receiver, QEvent *e) override
    {
        if (e->type() == QEvent::MouseButtonPress)
            std::printf("1. QApplication::notify       接收者 %s\n", receiver->metaObject()->className());
        return QApplication::notify(receiver, e);
    }
};

// 第 2 站：装在 QApplication 上的事件过滤器，能看到发给任何对象的事件
struct AppFilter : QObject {
    bool eventFilter(QObject *obj, QEvent *e) override
    {
        if (e->type() == QEvent::MouseButtonPress)
            std::printf("2. 应用程序的事件过滤器         %s\n", obj->metaObject()->className());
        return false;
    }
};

// 第 3 站：装在按钮上的事件过滤器
struct ObjFilter : QObject {
    bool block = false;
    bool eventFilter(QObject *, QEvent *e) override
    {
        if (e->type() == QEvent::MouseButtonPress) {
            std::printf("3. 按钮上的事件过滤器%s\n", block ? "，返回 true，拦截" : "");
            return block;
        }
        return false;
    }
};

// 第 4、5 站：按钮自己的 event() 和 mousePressEvent()
class Button : public QPushButton
{
public:
    using QPushButton::QPushButton;
protected:
    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::MouseButtonPress)
            std::printf("4. Button::event()\n");
        return QPushButton::event(e);
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        std::printf("5. Button::mousePressEvent()\n");
        QPushButton::mousePressEvent(e);
    }
};
```

```cpp
App app(argc, argv);
AppFilter af;
app.installEventFilter(&af);
Button button("按钮");
ObjFilter of;
button.installEventFilter(&of);
QObject::connect(&button, &QPushButton::pressed, [] { std::printf("6. pressed 信号\n"); });
// 分别在不拦截和拦截时，发送一次鼠标按下
```

```
---- 不拦截
1. QApplication::notify       接收者 QPushButton
2. 应用程序的事件过滤器         QPushButton
3. 按钮上的事件过滤器
4. Button::event()
5. Button::mousePressEvent()
6. pressed 信号
---- 按钮的过滤器拦截
1. QApplication::notify       接收者 QPushButton
2. 应用程序的事件过滤器         QPushButton
3. 按钮上的事件过滤器，返回 true，拦截
```

画成图：

```
事件循环取出一个事件
  │
  ▼
QApplication::notify(接收者, 事件)       ← 1. 所有事件的总入口
  │
  ├─► QApplication 上的事件过滤器        ← 2. 返回 true 就到此为止
  ├─► 接收者上的事件过滤器               ← 3. 返回 true 就到此为止
  │      （装了多个时，后装的先调用）
  ▼
接收者->event(事件)                     ← 4. 按事件类型分派
  │
  ▼
mousePressEvent / keyPressEvent / paintEvent ……   ← 5. 具体的处理函数
  │
  ▼
控件自己决定发出什么信号（pressed、clicked、textChanged ……）   ← 6.
```

拦截的那一次，事件到第 3 站就停了：按钮没有收到鼠标按下，也就不会发出 `pressed` 信号。对用户来说，这个按钮「点不动」了。

**事件**和**信号**的区别也就清楚了：事件是从外部（系统、Qt、其他对象）**发给**一个对象的，对象在处理函数中决定怎样响应；信号是对象处理完事件之后**发出来**的，告诉别人「发生了什么」。写控件的人处理事件，用控件的人连接信号。

### 1.2 该在哪一站插手

| 位置 | 能拦截的范围 | 用途 |
|---|---|---|
| 重写 `QApplication::notify` | 程序中所有对象的所有事件 | 很少用。例如统一捕获事件处理中抛出的异常 |
| `QApplication` 上的事件过滤器 | 同上 | 全局快捷键、统计用户的空闲时间（任何键盘鼠标事件都重新计时）、上一篇的调色板变化 |
| 对象上的事件过滤器 | 一个对象 | 不想（或不能）继承一个控件，又要改变它的行为。例如让一个 `QLineEdit` 按上下键时切换历史记录 |
| 重写 `event()` | 自己的类 | 处理没有专门处理函数的事件，例如第 3 节的自定义事件 |
| 重写 `xxxEvent()` | 自己的类 | 最常用。第二章的开关控件就是这样写的 |

越靠前，影响的范围越大，代价也越大：装在 `QApplication` 上的过滤器，**每一个事件**都要调用它一次，所以里面的代码必须非常快，不感兴趣的事件要尽早 `return false`。

## 2. accept、ignore 与事件传播

### 2.1 鼠标事件会传给父控件

鼠标点击发给的是鼠标下面**最深**的那个控件。如果这个控件不想处理呢？比如窗口上有一个面板，面板上有一个标签，用户点了标签，可标签对点击没有反应，真正关心点击的是外面的窗口（比如点空白处取消选择）。

每个事件都有一个「已接受」的标志：处理函数调用 `event->accept()` 表示「我处理了」，调用 `event->ignore()` 表示「我不管」。鼠标和键盘事件被忽略时，Qt 会把它**传给父控件**，直到有控件接受，或者到了顶层窗口。

搭一个三层的结构：窗口 → 面板 → 标签，每一层都打印收到的鼠标按下，面板总是忽略，窗口总是接受，标签分别试一下忽略和接受：

```cpp
template <class Base>
class Logged : public Base
{
public:
    Logged(const char *name, bool accept) : m_name(name), m_accept(accept) {}
protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        std::printf("  %s 收到，位置 (%.0f, %.0f)，%s\n", m_name, e->position().x(), e->position().y(),
                    m_accept ? "accept()" : "ignore()");
        if (m_accept) e->accept(); else e->ignore();
    }
private:
    const char *m_name;
    bool m_accept;
};

Logged<QWidget> window("窗口 window", true);
auto *panel = new Logged<QWidget>("面板 panel", false);
auto *label = new Logged<QLabel>("标签 label", mode == 1);
// window 的边距 20，panel 的边距 10，label 在 panel 中，panel 在 window 中
```

`Logged` 是一个类模板，继承自模板参数 `Base`，这样同一份打印代码可以同时用在 `QWidget` 和 `QLabel` 上（C++基础专栏《模板入门》）。

```
---- 标签忽略事件
  鼠标下面的控件: label
  标签 label 收到，位置 (5, 5)，ignore()
  面板 panel 收到，位置 (15, 15)，ignore()
  窗口 window 收到，位置 (35, 35)，accept()
  最终 isAccepted = 1
---- 标签接受事件
  鼠标下面的控件: label
  标签 label 收到，位置 (5, 5)，accept()
  最终 isAccepted = 1
```

- 标签忽略后，事件依次传给了面板、窗口，窗口接受了，传播停止。
- 每传一层，**鼠标位置都换算成了接收者自己的坐标**：标签内的 (5, 5)，在面板中是 (15, 15)（加上面板的边距 10），在窗口中是 (35, 35)（再加上窗口的边距 20）。
- 标签接受时，面板和窗口都不知道发生了这次点击。

多数控件的默认行为是：自己用得上的事件就接受（按钮接受点击，输入框接受按键），用不上的就忽略（`QLabel`、`QWidget` 默认忽略鼠标点击）。所以在一个按钮上点击，外面的窗口收不到；在一个标签上点击，外面的窗口能收到。

键盘事件也一样：输入框不处理的按键（例如 Esc），会传给它所在的对话框，对话框把 Esc 当作「取消」。第二章第 2.1 篇说「按 Esc 相当于 `reject()`」，就是这样实现的。

### 2.2 不是所有事件都传播

只有「输入类」的事件会传给父控件：鼠标、键盘、滚轮、触摸、拖放。`Paint`、`Resize`、`Show` 这些事件是发给某一个控件**本身**的，没有「这个控件不处理，交给父控件」的意思，忽略它们也不会传播。

另外，对 `QCloseEvent`（用户点了窗口的关闭按钮）调用 `ignore()` 的含义是「不允许关闭」，常用来在关闭前询问是否保存：

```cpp
void Editor::closeEvent(QCloseEvent *event)
{
    if (!isModified() || askSave())    // askSave 弹出第二章 2.1 篇的 QMessageBox
        event->accept();
    else
        event->ignore();               // 用户点了「取消」，窗口不关
}
```

## 3. 自己发送事件

### 3.1 sendEvent 和 postEvent

程序也可以自己发送事件，有两种方式：

- **`QCoreApplication::sendEvent(接收者, 事件)`**：立即走一遍第 1 节的路径，处理完才返回。事件对象通常在栈上创建。前两章模拟鼠标点击、键盘输入，用的都是它。
- **`QCoreApplication::postEvent(接收者, 事件)`**：把事件放进事件队列，立即返回，回到事件循环时才处理。事件对象**必须用 `new` 创建**，处理完后由 Qt 删除。

### 3.2 自定义事件

事件类型可以自己定义，用 `QEvent::registerEventType()` 申请一个不会和别人冲突的编号：

```cpp
// 自定义事件类型
const QEvent::Type Ping = QEvent::Type(QEvent::registerEventType());

class Receiver : public QObject
{
public:
    int count = 0;
protected:
    bool event(QEvent *e) override
    {
        if (e->type() == Ping) {
            ++count;
            std::printf("  Receiver 处理 Ping #%d\n", count);
            return true;
        }
        return QObject::event(e);       // 其他事件交给基类
    }
};
```

自定义事件没有专门的处理函数，要在 `event()` 中处理。处理了就返回 `true`；不认识的事件一定要交给基类的 `event()`，否则这个对象就收不到定时器等其他事件了。

```cpp
Receiver r;
std::printf("Ping 的类型编号 %d（QEvent::User = %d）\n", Ping, QEvent::User);

std::printf("sendEvent 之前\n");
QEvent e1(Ping);
QCoreApplication::sendEvent(&r, &e1);         // 立即处理，事件对象在栈上
std::printf("sendEvent 之后\n");

std::printf("postEvent 之前\n");
QCoreApplication::postEvent(&r, new QEvent(Ping));   // 放进队列，必须 new，由 Qt 删除
QCoreApplication::postEvent(&r, new QEvent(Ping));
std::printf("postEvent 之后，已处理 %d 个\n", r.count);
QTimer::singleShot(0, [&] { std::printf("回到事件循环后，已处理 %d 个\n", r.count); app.quit(); });
```

```
Ping 的类型编号 65535（QEvent::User = 1000）
sendEvent 之前
  Receiver 处理 Ping #1
sendEvent 之后
postEvent 之前
postEvent 之后，已处理 1 个
  Receiver 处理 Ping #2
  Receiver 处理 Ping #3
回到事件循环后，已处理 3 个
```

- `sendEvent` 的处理发生在「之前」和「之后」两行之间，就是一次函数调用。
- 两次 `postEvent` 之后，计数还是 1；回到事件循环，两个事件才按顺序被处理。
- `registerEventType()` 从 65535 开始往下分配编号，`QEvent::User`（1000）到 65535 都是留给程序自定义的。

`postEvent` 的「放进队列，以后再处理」，和第一章的 `deleteLater()`、第二章的 `update()` 是同一个机制：`deleteLater()` 投递的是一个 `DeferredDelete` 事件，`update()` 投递的是一个 `UpdateRequest` 事件。更重要的是，**`postEvent` 是线程安全的**：可以在另一个线程中把事件投递给界面线程的对象，事件会在界面线程中处理。下一篇的多线程就建立在这个机制之上。

## 4. 不要阻塞事件循环

### 4.1 卡住 1 秒是什么样子

第一章就说过：界面上的一切都靠事件循环推动，事件处理函数和槽必须很快返回。现在量一下违反这条规则的后果：用一个每 50 毫秒触发一次的定时器作为「心跳」，如果两次心跳的间隔超过 80 毫秒，就说明事件循环被卡住了。300 毫秒时，在一个槽中做一件耗时 1 秒的事：

```cpp
QTimer heartbeat;
heartbeat.setTimerType(Qt::PreciseTimer);
QObject::connect(&heartbeat, &QTimer::timeout, [&] {
    qint64 now = clock.elapsed();
    if (now - last > 80)
        std::printf("%5lld ms  心跳，距上一次 %lld ms  ← 事件循环被卡住了\n", now, now - last);
    last = now;
});
heartbeat.start(50);

QTimer::singleShot(300, [&] {
    std::printf("%5lld ms  开始一个耗时 1 秒的操作\n", clock.elapsed());
    std::this_thread::sleep_for(std::chrono::seconds(1));   // 代表任何耗时的计算或阻塞的 IO
    std::printf("%5lld ms  操作结束\n", clock.elapsed());
});
```

```
  301 ms  开始一个耗时 1 秒的操作
 1302 ms  操作结束
 1303 ms  心跳，距上一次 1002 ms  ← 事件循环被卡住了
```

心跳停了 1002 毫秒。这期间本该有 20 次心跳，一次都没有发生；同样，用户的点击、按键、窗口的重画，也都在队列里排着队。用户看到的是：窗口不动了，按钮按不下去，拖动窗口时里面一片空白。卡住超过几秒，macOS 会显示旋转的彩色光标，Windows 会在标题栏上显示「未响应」。

`sleep_for` 代表任何**在界面线程中耗时的事**：

- 大量计算：排序、解析一个大文件、生成缩略图、第二章 2.2 篇那次 31 秒的排序；
- 阻塞的 IO：同步地读写大文件、同步的网络请求（`recv` 一直等到数据到来）、同步的数据库查询；
- 等待：`sleep`、等另一个线程的锁。

一般的经验是：用户点击后 100 毫秒内有反应，会觉得是「立即」的；超过 1 秒，就会觉得程序卡了。

### 4.2 processEvents：看起来能解决

一个常见的「解决办法」是在耗时的循环中定时调用 `QApplication::processEvents()`，让事件循环转一下：

```cpp
void longTask(int id)
{
    ++depth;
    std::printf("%5lld ms  任务 %d 开始，当前嵌套深度 %d\n", clock_.elapsed(), id, depth);
    for (int i = 0; i < 10; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        QApplication::processEvents();          // 让界面在循环中途也能响应
    }
    std::printf("%5lld ms  任务 %d 结束\n", clock_.elapsed(), id);
    --depth;
}
```

每 100 毫秒处理一次积压的事件，界面确实不会完全卡死了。但这样一来，在任务进行期间**用户可以继续操作**，包括再点一次「开始」按钮。模拟用户在 0 毫秒和 300 毫秒各点了一次：

```cpp
QTimer::singleShot(0, [] { longTask(1); });
QTimer::singleShot(300, [] { longTask(2); });
```

```
   25 ms  任务 1 开始，当前嵌套深度 1
  337 ms  任务 2 开始，当前嵌套深度 2
 1347 ms  任务 2 结束
 2056 ms  任务 1 结束
```

任务 2 不是在任务 1 之后执行的，而是在任务 1 的 `processEvents()` **里面**执行的：

```
事件循环
└── longTask(1)
    ├── 循环第 1、2、3 次
    └── processEvents()
        └── longTask(2)            ← 嵌套在任务 1 中间
            ├── 循环 10 次，1 秒
            └── 返回
    ├── 循环第 4～10 次           ← 任务 2 结束后才能继续
    └── 返回
```

后果是：

- 任务 1 本来 1 秒就能完成，实际用了 2 秒，而且**必须等任务 2 先结束**。用户点得越多，嵌套越深，最早的任务越晚完成。
- `longTask` 被**重入**了：同一个函数在调用栈上出现了两次。如果它修改了成员变量（比如一个「正在处理的文件列表」），两次执行会互相干扰。
- 和第二章 2.1 篇的 `exec()` 一样，`processEvents()` 中可能发生任何事：窗口被关闭、对象被删除，回到循环后访问的可能是已经释放的内存。

`processEvents()` 本质上就是手动写了一个嵌套的事件循环，嵌套事件循环的所有风险它都有。可以在很简单的场合临时用一下（比如程序启动时显示一个进度），但它不是让界面不卡的正确方法。

正确的方法是：**把耗时的工作放到另一个线程中去**，界面线程只负责显示和响应用户；工作完成后，用信号或者投递事件通知界面线程更新。下一篇讲 `QThread`。

## 5. 小结

- 一个事件依次经过：`QApplication::notify` → `QApplication` 上的事件过滤器 → 对象上的事件过滤器 → `event()` → 具体的处理函数（`mousePressEvent` 等）。过滤器返回 `true` 就拦截了事件。控件处理事件后发出信号。
- 越靠前的拦截点影响范围越大；常用的是对象上的事件过滤器和重写处理函数。
- 鼠标、键盘等输入事件被 `ignore()` 时会传给父控件，位置换算成父控件的坐标，直到有控件接受。绘制、大小改变等事件不传播。`QCloseEvent` 被忽略表示不允许关闭。
- `sendEvent` 立即处理；`postEvent` 放进队列，回到事件循环才处理，事件对象要 `new`，而且可以跨线程投递。`deleteLater`、`update` 都基于投递事件。
- 在界面线程中做耗时的事会让事件循环停转，界面完全无响应。`processEvents()` 能缓解，但会导致重入和嵌套，任务的完成顺序也会被打乱。耗时的工作应该放到其他线程。

## 练习

1. 用 `QApplication` 上的事件过滤器实现「用户空闲 5 秒后在状态栏显示『离开』」：收到任何鼠标或键盘事件时重新计时。过滤器中应该怎样尽快放过不关心的事件？
2. 给第二章 2.1 篇的登录对话框中的用户名输入框装一个事件过滤器：按 ↑ 键时填入上一次登录的用户名。
3. 在第 2.1 节的实验中，把窗口也改成 `ignore()`，`press.isAccepted()` 的最终结果是什么？
4. 用 `postEvent` 在一个循环中连续投递 10 万个自定义事件，测量投递用了多久、全部处理完又用了多久。事件队列占用了多少内存（可以用第二章 2.2 篇的 `rssMB()`）？
