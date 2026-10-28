---
title: 界面卡顿与 QThread
date: 2026-10-28 14:00:00
updated: 2026-10-28 14:00:00
categories:
  - 客户端开发
  - 事件与多线程
tags:
  - Qt
  - C++
  - 多线程
  - QThread
order: 2
description: 把上一篇卡住界面 1 秒的工作搬进一个工作对象，用 moveToThread 移到另一个线程：doWork 在「工作线程」中执行，进度和结果信号却在「界面线程」中处理，界面的心跳最大间隔只有 51 毫秒。继承 QThread 是另一种写法，可是它的槽并不在新线程中运行，打印出来全是「界面线程」，因为 QThread 对象本身属于创建它的线程。在工作线程中启动界面的定时器、给界面的对象创建子对象，Qt 会警告并拒绝；在工作线程中修改 QLabel 20 万次，10 次运行都没有崩溃，Qt 也没有任何警告，可这仍然是错的。最后看线程的退出：运行着事件循环的 QThread 在析构时还没结束，程序 abort，退出码 134。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会用「工作对象 + `moveToThread`」把耗时的工作放到另一个线程，用信号把进度和结果送回界面线程；理解对象的**线程归属**；知道继承 `QThread` 的写法和它的常见误解；知道哪些操作只能在界面线程中做；会正确地停止线程。
>
> **前置知识**：上一篇；C++11 专栏《多线程入门：std::thread 与互斥锁》，Linux 基础专栏《POSIX 线程》《线程同步》。
>
> **实验环境**：macOS 27.0（Apple M6，12 核），Qt 6.12.0，Release 构建。

## 1. 线程与对象的归属

上一篇的结论是：耗时的工作要放到另一个线程中，界面线程只负责界面。C++11 已经有了 `std::thread`，为什么还要用 Qt 的线程？

关键在于**怎样把结果送回界面**。工作线程算完了，要把结果显示在一个 `QLabel` 上，可是界面上的对象只能在界面线程中使用（第 4 节）。用 `std::thread`，就要自己写一个线程安全的队列，再想办法让界面线程去取。Qt 已经有这样一个队列了：上一篇的事件队列，`postEvent` 是线程安全的。Qt 的线程和信号与槽结合起来，自动利用这个队列。

为此，Qt 中每个 `QObject` 都**属于**一个线程，叫作对象的**线程归属**（thread affinity）：

- 对象默认属于**创建它的线程**，`obj->thread()` 返回这个线程；
- 一个线程有自己的事件循环，发给对象的事件（包括第 3 节说的跨线程信号）在对象**所属线程**的事件循环中处理；
- 可以用 `obj->moveToThread(另一个线程)` 改变对象的归属。

**`QThread`** 是 Qt 对一个线程的封装。注意，`QThread` 对象本身**不是**那个线程，它只是管理线程的一个对象，它自己属于创建它的线程（第 3 节会看到这一点带来的误解）。`QThread::start()` 启动线程后，新线程默认运行一个事件循环（`QThread::run()` 默认调用 `exec()`），等待处理发给它的事件。

## 2. 工作对象 + moveToThread

### 2.1 写一个工作对象

把耗时的工作写成一个普通的 `QObject` 子类的槽，进度和结果用信号发出：

```cpp
#include <QObject>
#include <QThread>

// 当前线程的名字（返回 QByteArray，调用处用 .data()，避免指向临时对象）
inline QByteArray threadName()
{
    return QThread::currentThread()->objectName().toUtf8();
}

// 在工作线程中运行的对象：收到任务，算完后发出结果
class Worker : public QObject
{
    Q_OBJECT
public slots:
    void doWork(int n);
signals:
    void progress(int percent);
    void finished(long long result);
};

void Worker::doWork(int n)
{
    std::printf("%5lld ms  [%s] doWork(%d) 开始\n", clock_.elapsed(), threadName().data(), n);
    long long sum = 0;
    for (int i = 1; i <= 10; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));   // 模拟耗时的计算
        sum += i * n;
        emit progress(i * 10);
    }
    std::printf("%5lld ms  [%s] doWork 结束\n", clock_.elapsed(), threadName().data());
    emit finished(sum);
}
```

`threadName()` 返回 `QByteArray` 而不是 `const char *`，是有原因的：最初这里写的是 `return qPrintable(...)`，结果打印出来的线程名全是空的。`qPrintable` 返回的指针指向一个临时的 `QByteArray`，函数返回时这个临时对象就析构了，指针悬空（第一章《对象树与内存管理》的同一类错误，只是这次在 Qt 的工具函数上）。

`Worker` 里没有任何和线程有关的代码，它不知道自己会在哪个线程中运行。这正是这种写法的好处：工作的逻辑和线程管理分开了。

### 2.2 移到工作线程中

```cpp
QThread::currentThread()->setObjectName("界面线程");

QThread thread;
thread.setObjectName("工作线程");
auto *worker = new Worker;                 // 不能有父对象，才能移动到另一个线程
worker->moveToThread(&thread);
QObject::connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
thread.start();

QObject::connect(worker, &Worker::progress, &app, [](int p) {
    if (p % 50 == 0)
        std::printf("%5lld ms  [%s] 进度 %d%%\n", clock_.elapsed(), threadName().data(), p);
});
QObject::connect(worker, &Worker::finished, &app, [&](long long r) {
    std::printf("%5lld ms  [%s] 收到结果 %lld，心跳最大间隔 %lld ms\n",
                clock_.elapsed(), threadName().data(), r, maxGap);
    thread.quit();                         // 让工作线程的事件循环退出
    thread.wait();                         // 等它真正结束
    app.quit();
});

QTimer::singleShot(200, [&] {
    std::printf("%5lld ms  [%s] 请求开始工作\n", clock_.elapsed(), threadName().data());
    QMetaObject::invokeMethod(worker, &Worker::doWork, 7);    // 跨线程调用：在 worker 的线程中执行
    std::printf("%5lld ms  [%s] invokeMethod 立即返回\n", clock_.elapsed(), threadName().data());
});
```

同时运行上一篇的心跳，每 50 毫秒检查一次界面线程的事件循环：

```
  201 ms  [界面线程] 请求开始工作
  202 ms  [界面线程] invokeMethod 立即返回
  202 ms  [工作线程] doWork(7) 开始
  721 ms  [界面线程] 进度 50%
 1242 ms  [工作线程] doWork 结束
 1242 ms  [界面线程] 进度 100%
 1242 ms  [界面线程] 收到结果 385，心跳最大间隔 51 ms
```

- `invokeMethod` 在界面线程中**立即返回**，`doWork` 在**工作线程**中执行。
- `doWork` 在工作线程中 `emit progress(...)`，处理进度的 lambda 却在**界面线程**中执行：因为 lambda 的上下文对象是 `&app`，属于界面线程。Qt 发现发送者和接收者不在同一个线程，就把这次调用打包成一个事件，投递到接收者所在线程的事件队列中。
- 整个过程中，心跳的最大间隔只有 51 毫秒：界面线程一直在正常地转动。上一篇同样 1 秒的工作，把心跳卡住了 1002 毫秒。

所以在接收结果的槽中，**可以放心地修改界面**，它就在界面线程中执行。这就是 Qt 多线程最常用的模式：

```
界面线程                                工作线程
────────                                ────────
用户点击「开始」
  invokeMethod(worker, doWork) ──事件──► doWork() 开始
（立即返回，继续处理界面事件）              │  耗时的计算
                                         │  emit progress(50)
  进度条更新到 50% ◄────────────事件──────┘
                                         │  ……
  显示结果 ◄────────────────────事件────── emit finished(结果)
```

### 2.3 几个细节

- **`moveToThread` 的对象不能有父对象**：父子对象必须在同一个线程中，否则对象树的删除就要跨线程了。所以 `worker` 用 `new Worker` 创建，不传父对象。
- **怎样删除 worker**：它属于工作线程，应该在工作线程中删除。把 `QThread::finished` 连接到 `worker` 的 `deleteLater`，线程结束时，worker 在工作线程的最后一轮事件中被删除。
- **为什么用 `invokeMethod` 而不是直接调用**：直接写 `worker->doWork(7)` 就是一次普通的函数调用，`doWork` 会在**界面线程**中执行，界面照样卡住。实际的程序中，通常是把界面上的某个信号（例如「开始」按钮的 `clicked`）连接到 `worker` 的 `doWork` 槽，效果和 `invokeMethod` 一样。
- **工作线程中正在执行 `doWork` 时，它的事件循环是停着的**：这时再给 worker 投递任务，任务会在队列中等着，`doWork` 返回后才处理。所以一个 worker 天然地**一次只做一件事**，任务按顺序执行。

## 3. 继承 QThread

另一种写法是继承 `QThread`，重写 `run()`，`run()` 中的代码在新线程中执行。很多教程和老代码都这样写，但有一个常见的误解。

```cpp
// 常见的误解：继承 QThread，以为它的槽也在新线程中执行
class MyThread : public QThread
{
public:
    void onPing() { std::printf("  MyThread::onPing()  在 [%s]\n", threadName().data()); }
protected:
    void run() override
    {
        std::printf("  MyThread::run()     在 [%s]\n", threadName().data());
        exec();                                  // 运行这个线程自己的事件循环
    }
};

MyThread t;
t.setObjectName("新线程");
t.start();

QTimer timer;
QObject::connect(&timer, &QTimer::timeout, &t, &MyThread::onPing);
// 启动 timer，每 10 ms 触发一次
std::printf("  t 这个对象属于 [%s]\n", t.thread()->objectName().toUtf8().data());
```

```
  MyThread::run()     在 [新线程]
  MyThread::onPing()  在 [界面线程]
  MyThread::onPing()  在 [界面线程]
  t 这个对象属于 [界面线程]
  MyThread::onPing()  在 [界面线程]
```

`run()` 在新线程中，`onPing()` 却在界面线程中。原因就是第 1 节说的：**`MyThread` 这个对象是在界面线程中创建的，它属于界面线程**，所以连接到它的槽在界面线程中执行。只有 `run()` 是新线程的入口函数。

如果 `onPing` 和 `run` 访问同一个成员变量，它们就运行在两个不同的线程中，需要加锁。这个误解是很多「明明用了线程，界面还是卡」「偶尔数据错乱」问题的根源。

两种写法的选择：

| 写法 | 适用 |
|---|---|
| 工作对象 + `moveToThread` | 需要接收多个任务、需要事件循环（定时器、网络、跨线程信号）。推荐 |
| 继承 `QThread`，重写 `run()` | 一段从头跑到尾的循环，不需要接收任务，例如持续读取一个设备 |
| `QThread::create(函数)` | 同上，用 lambda 写，不用定义类 |
| `QtConcurrent::run` | 一次性的计算任务，用线程池，第 4 篇 |

## 4. 哪些事只能在界面线程中做

Qt 的文档规定：**`QWidget` 及其所有子类，以及 `QPixmap` 等和显示有关的类，只能在界面线程（主线程）中使用。** 原因是操作系统的窗口系统本身大多不是线程安全的（macOS 的 AppKit 要求界面操作在主线程中进行），控件内部的状态也没有加锁。

违反了会怎样？在工作线程中做四件「越界」的事：

```cpp
QWidget window;
QTimer uiTimer;                                  // 属于界面线程的定时器
window.show();

QThread *t = QThread::create([&] {
    std::printf("1. 在工作线程中启动界面线程的定时器\n");
    uiTimer.start(100);
    std::printf("2. 在工作线程中创建一个以 window 为父对象的 QObject\n");
    QObject *child = new QObject(&window);
    std::printf("   child->parent() = %p\n", (void *)child->parent());
    std::printf("3. 在工作线程中创建 QPixmap\n");
    QPixmap pm(100, 100);
    std::printf("   pm.isNull() = %d\n", pm.isNull());
    std::printf("4. 在工作线程中创建一个控件\n");
    QLabel *label = new QLabel("x");
    std::printf("   创建完成 %p\n", (void *)label);
});
t->start();
t->wait();
```

```
1. 在工作线程中启动界面线程的定时器
QObject::startTimer: Timers cannot be started from another thread
2. 在工作线程中创建一个以 window 为父对象的 QObject
QObject: Cannot create children for a parent that is in a different thread.
(Parent is QWidget(0x16d502608), parent's thread is QThread(0x105814160), current thread is QThread(0x76e245aa20)
   child->parent() = 0x0
3. 在工作线程中创建 QPixmap
   pm.isNull() = 0
4. 在工作线程中创建一个控件
   创建完成 0x76e2535230
```

- 前两件，Qt 检查到了：打印警告，**拒绝执行**。定时器没有启动；`child` 被创建了，但父对象是空的，它不在对象树中，泄漏了。程序继续运行，只有一行警告提示出了问题。
- 后两件，Qt 什么也没说，看起来都成功了。

再做一个实验：在工作线程中调用 `label.setText()` 20 万次，同时界面线程在不停地重画这个标签。运行 10 次，**一次也没有崩溃**，也没有任何警告。

这不说明它是对的。两个线程同时读写标签的文字，就是 C++11 专栏中说的**数据竞争**，属于未定义行为：可能一直没事，也可能在用户的电脑上、在某个特定的时机崩溃，或者显示出乱码。本来用 ThreadSanitizer 可以检查数据竞争，但这里竞争发生在 Qt 库的内部，而 Homebrew 安装的 Qt 没有用 ThreadSanitizer 编译，它看不到库内部的读写，同样什么也没报。

所以只能靠规则，而不能靠「运行没出错」：**工作线程只做计算和 IO，产生的结果通过信号送回界面线程，由界面线程去更新控件。** 第 2 节的写法天然满足这条规则。需要在工作线程中处理图片时，用 `QImage` 代替 `QPixmap`，`QImage` 可以在任何线程中使用。

## 5. 停止线程

### 5.1 正确的顺序

线程不会因为程序要退出就自动停止。第 2 节的代码在收到结果后，依次调用了：

```cpp
thread.quit();      // 请工作线程的事件循环退出（投递一个「退出」请求，立即返回）
thread.wait();      // 阻塞，直到线程真正结束
```

如果忘了呢？把这两行去掉，让 `QThread` 对象在线程还在运行时析构：

```cpp
{
    QThread thread;                 // run() 默认就是 exec()：运行一个事件循环
    thread.start();
    std::printf("离开作用域，thread 析构，没有调用 quit()\n");
}
std::printf("不会执行到这里\n");
```

```
离开作用域，thread 析构，没有调用 quit()
QThread: Destroyed while thread '' is still running
```

```bash
echo $?
```

```
134
```

程序直接 abort。`QThread` 析构时如果线程还在运行，就没有任何安全的处理办法：强行终止线程可能让它持有的锁永远不释放，等待它又可能永远等不到（这个线程的事件循环在等「退出」请求，而没有人发出这个请求），所以 Qt 选择立即终止程序，让错误暴露出来。

`QThread::create` 创建的线程有点不同：它的析构函数会先请求中断、再等待线程结束。用它创建一个睡眠 2 秒的线程，立即删除：

```
删除 QThread，线程还在运行
delete 返回，用了 2005 ms
```

`delete` 阻塞了 2 秒才返回。如果这发生在界面线程中，界面就卡住 2 秒。

如果线程中的代码定期检查中断请求（5.2 节），情况就好得多。把线程改成每 100 毫秒检查一次 `isInterruptionRequested()`，运行 300 毫秒后删除：

```
线程: 第 3 次检查时发现被请求中断
delete 用了 6 ms
```

线程收到中断请求，提前结束，`delete` 只等了 6 毫秒。所以无论用哪种方式创建线程，都应该让线程中的代码能够被及时地叫停。

### 5.2 中途取消

`quit()` 只能让事件循环退出，**不能**打断正在执行的 `doWork`：工作线程正在执行一个槽时，事件循环是停着的，「退出」请求要等 `doWork` 返回后才处理。用户点了「取消」，耗时 10 秒的计算还是要跑完 10 秒。

要中途取消，工作的代码自己必须定期检查一个标志：

```cpp
void Worker::doWork(int n)
{
    for (int i = 0; i < 1000; ++i) {
        if (QThread::currentThread()->isInterruptionRequested())
            return;                          // 被要求中断，提前结束
        // ……一小段计算
    }
}

// 界面线程：用户点了「取消」
thread.requestInterruption();
```

`requestInterruption()` 设置一个线程安全的标志，`isInterruptionRequested()` 读取它。也可以用一个 `std::atomic<bool>`（C++11 专栏《原子操作与内存序》）。检查得越频繁，取消的反应越快；检查本身很便宜，每次循环检查一次就可以。

程序退出时的完整步骤：

```cpp
thread.requestInterruption();   // 1. 请正在执行的工作尽快结束
thread.quit();                  // 2. 请事件循环退出
thread.wait();                  // 3. 等待线程结束
```

## 6. 小结

- 每个 `QObject` 属于一个线程（默认是创建它的线程），发给它的事件和跨线程的信号在它所属线程的事件循环中处理。`moveToThread` 改变归属。
- 推荐的写法：工作的逻辑写在一个普通的 `QObject` 子类中，`moveToThread` 到一个 `QThread`，用信号（或 `invokeMethod`）把任务交给它，结果通过信号送回界面线程。界面线程不会被阻塞。
- `QThread` 对象本身属于创建它的线程。继承 `QThread` 时只有 `run()` 在新线程中执行，它的槽仍在原来的线程中执行。
- 控件、`QPixmap` 等只能在界面线程中使用。Qt 只检查了其中一部分（定时器、父对象），检查不到的越界操作即使运行正常，也是数据竞争。工作线程处理图片用 `QImage`。
- 线程要显式地停止：`requestInterruption()` 让正在执行的工作提前结束，`quit()` 让事件循环退出，`wait()` 等待结束。运行中的 `QThread` 被析构会导致程序 abort。

第 2 节中，工作线程的信号自动到了界面线程，这是 Qt 根据发送者和接收者所在的线程决定的。下一篇详细看这个决定是怎样做的：信号与槽的几种连接方式，以及跨线程传递数据的开销和陷阱。

## 练习

1. 给第 2 节的程序加一个「取消」：在 500 毫秒时调用 `thread.requestInterruption()`，修改 `doWork` 让它提前结束。确认 `finished` 信号还会发出吗？应该发出吗？
2. 在 `doWork` 还在执行时，再调用两次 `invokeMethod(worker, &Worker::doWork, ...)`，打印三个任务开始和结束的时间。它们是并行执行的吗？
3. 把第 3 节的 `MyThread` 改成在构造函数中调用 `moveToThread(this)`，`onPing` 会在哪个线程中执行？为什么很多人不推荐这样做？
4. 把第 2 节程序中的 `thread.wait()` 去掉，只保留 `quit()`，程序还能正常退出吗？多运行几次看看。
