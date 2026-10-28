---
title: 跨线程的信号与槽
date: 2026-10-28 15:00:00
updated: 2026-10-28 15:00:00
categories:
  - 客户端开发
  - 事件与多线程
tags:
  - Qt
  - C++
  - 多线程
  - 信号与槽
order: 3
description: 同一条连接，在界面线程中 emit 时，槽被排进工作线程的队列，emit 立即返回；在工作线程中 emit 时，槽被直接调用。默认的 AutoConnection 是在每次 emit 时、根据发出信号的线程和接收者所在的线程决定的。本文逐一实测五种连接方式：DirectConnection 让工作线程的对象在界面线程中执行槽；同一线程中的 QueuedConnection 也要等回到事件循环；同一线程中的 BlockingQueuedConnection，Qt 打印出「Dead lock detected」之后，程序还是卡死了。跨线程传递 4000 万字节的 QByteArray 用了 0.34 毫秒，接收方拿到的是同一块内存；同样大小的 std::vector 被拷贝了一份，用了 5.9 毫秒。生产者 80 毫秒发出 100 万条消息，消费者要 15 秒才处理完，队列让内存多了 361 MB。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解五种连接方式：Auto、Direct、Queued、BlockingQueued、SingleShot / Unique 标志；知道 AutoConnection 是怎样、在什么时候做决定的；理解跨线程连接中参数的拷贝，知道隐式共享的类型为什么便宜；知道队列连接的死锁和堆积问题，以及怎样避免。
>
> **前置知识**：上一篇《界面卡顿与 QThread》；第一章《信号与槽》。
>
> **实验环境**：macOS 27.0（Apple M6），Qt 6.12.0，Release 构建。本篇的程序都只用 Qt Core。

## 1. 五种连接方式

第一章说「同一个线程中，`emit` 就是函数调用」，上一篇又看到工作线程中 `emit` 的信号，槽却在界面线程中执行。决定这一点的是 `connect` 的第五个参数，**连接方式**：

```cpp
QObject::connect(sender, &Sender::sig, receiver, &Receiver::slot, Qt::QueuedConnection);
```

| 方式 | 槽在哪个线程执行 | `emit` 何时返回 |
|---|---|---|
| `Qt::AutoConnection`（默认） | emit 时判断：同一线程就用 Direct，否则用 Queued | 见下文 |
| `Qt::DirectConnection` | **发出信号的线程** | 槽执行完 |
| `Qt::QueuedConnection` | **接收者所属的线程**，回到它的事件循环时 | 立即 |
| `Qt::BlockingQueuedConnection` | 接收者所属的线程 | 槽执行完（发送方阻塞等待） |
| `Qt::SingleShotConnection`（标志） | 和其他方式组合使用，触发一次后自动断开 | |

`Qt::UniqueConnection` 也是一个可以组合的标志：同样的连接已经存在时，不再重复连接。

## 2. 实测每一种

接收者 `r` 移到工作线程，发送者 `s` 留在界面线程，接收者的槽打印自己在哪个线程中执行：

```cpp
class Receiver : public QObject
{
    Q_OBJECT
public slots:
    void onPing(int n)
    {
        std::printf("    onPing(%d) 在 [%s]\n", n, threadName().data());
    }
};

QThread worker;
worker.setObjectName("工作线程");
worker.start();
Sender s;                                   // 属于界面线程
Receiver r;
r.moveToThread(&worker);                    // 属于工作线程
```

### 2.1 AutoConnection：每次 emit 时决定

```cpp
std::printf("1. AutoConnection，在界面线程中 emit\n");
auto c = QObject::connect(&s, &Sender::ping, &r, &Receiver::onPing);   // 默认就是 Auto
emit s.ping(1);
std::printf("   emit 返回\n");

std::printf("2. 同一个连接，在工作线程中 emit\n");
QMetaObject::invokeMethod(&r, [&] {          // 让这个 lambda 在工作线程中执行
    emit s.ping(2);
    std::printf("   emit 返回（在 [%s]）\n", threadName().data());
});
```

```
1. AutoConnection，在界面线程中 emit
   emit 返回
    onPing(1) 在 [工作线程]
2. 同一个连接，在工作线程中 emit
    onPing(2) 在 [工作线程]
   emit 返回（在 [工作线程]）
```

同一条连接，两次的行为不一样：

- 第 1 次，在界面线程中 `emit`，而接收者属于工作线程，用的是队列方式：`emit` 先返回，槽稍后在工作线程中执行。
- 第 2 次，在工作线程中 `emit`，和接收者在同一个线程，用的是直接方式：槽先执行，`emit` 才返回。

注意 AutoConnection 比较的是**执行 `emit` 的线程**和**接收者所属的线程**，而**不是**发送者对象所属的线程。第 2 次的发送者 `s` 属于界面线程，但 `emit` 发生在工作线程中，所以是直接调用。而且这个判断是在**每次 `emit` 时**做的，不是在 `connect` 时，所以 `connect` 之后再 `moveToThread` 也没问题。

### 2.2 DirectConnection：在发送方的线程中执行

```cpp
c = QObject::connect(&s, &Sender::ping, &r, &Receiver::onPing, Qt::DirectConnection);
emit s.ping(3);
```

```
3. DirectConnection，在界面线程中 emit
    onPing(3) 在 [界面线程]
   emit 返回
```

`r` 属于工作线程，它的槽却在界面线程中执行了。如果工作线程此时也在访问 `r` 的成员，就是两个线程同时访问同一个对象，需要自己加锁。跨线程时强制使用 DirectConnection，就放弃了 Qt 替我们做的线程切换，一般只在明确知道槽是线程安全的时候才用。

### 2.3 BlockingQueuedConnection：在对方线程中执行，并等它执行完

```cpp
c = QObject::connect(&s, &Sender::ping, &r, &Receiver::onPing, Qt::BlockingQueuedConnection);
emit s.ping(4);
std::printf("   emit 返回（槽已经执行完了）\n");
```

```
4. BlockingQueuedConnection，在界面线程中 emit
    onPing(4) 在 [工作线程]
   emit 返回（槽已经执行完了）
```

槽在工作线程中执行，`emit` 一直等到它执行完才返回。适合「需要对方先处理完，我才能继续」的场合，例如工作线程需要界面线程先确认一件事。

### 2.4 同一线程中的队列连接

发送者和接收者都在界面线程，强制使用队列方式：

```cpp
Receiver local;                             // 属于界面线程
c = QObject::connect(&s, &Sender::ping, &local, &Receiver::onPing, Qt::QueuedConnection);
emit s.ping(5);
std::printf("   emit 返回\n");
```

```
5. QueuedConnection，发送者和接收者都在界面线程
   emit 返回
    onPing(5) 在 [界面线程]
```

`emit` 先返回，槽在回到事件循环后才执行。这和第一章的 `deleteLater()`、上一篇的 `postEvent` 是一个道理：在同一线程中用队列连接，可以**把槽推迟到当前的处理结束之后**。比如第一章「在槽中删除发送者」的问题，也可以把连接改成队列方式来解决：等发送者的函数全部返回后，槽才执行。

### 2.5 同一线程中的阻塞队列连接：死锁

```cpp
QObject::connect(&s, &Sender::ping, &local, &Receiver::onPing, Qt::BlockingQueuedConnection);
emit s.ping(6);
std::printf("   emit 返回\n");
```

```
6. BlockingQueuedConnection，发送者和接收者都在界面线程
Qt: Dead lock detected while activating a BlockingQueuedConnection: Sender is Sender(0x16b306630), receiver is Receiver(0x16b306608)
```

```bash
echo $?
```

```
142
```

`emit` 把槽排进界面线程的队列，然后阻塞，等槽执行完；可是槽要等界面线程回到事件循环才能执行，而界面线程正阻塞在 `emit` 中。两边互相等待，就是**死锁**（Linux 基础专栏《线程同步》）。Qt 检测到了这个情况，打印了一行警告，但是**并没有阻止它**，程序就此卡死。实验中给程序设了 5 秒的超时，142 = 128 + 14，是超时的 `SIGALRM` 信号终止了它。

用 BlockingQueuedConnection 时一定要确认：发送方和接收方**一定**不在同一个线程中。如果用的是默认的 AutoConnection，就不会出现这个问题。

## 3. 参数是怎样跨线程的

### 3.1 队列连接要拷贝参数

直接连接时，参数就是普通的函数参数，`const T &` 直接引用发送方的对象。队列连接不行：`emit` 已经返回了，发送方的局部变量可能已经销毁，所以 Qt 必须把参数**拷贝**一份放进事件中，等槽执行时再交给它。

拷贝的代价取决于类型。分别跨线程发送三种 40 MB 左右的数据：`QByteArray`、`std::vector<int>`，和一个自定义的结构体 `Frame`，测量 `emit` 的用时，并检查接收方拿到的数据和发送方是不是**同一块内存**：

```cpp
struct Frame {                      // 一个自定义类型：一帧数据
    int id = 0;
    std::vector<int> samples;
    Frame() = default;
    Frame(const Frame &o) : id(o.id), samples(o.samples) { ++copies; }
    Frame &operator=(const Frame &) = default;
    static inline int copies = 0;
};

class Sender : public QObject
{
    Q_OBJECT
signals:
    void bytes(const QByteArray &data);
    void vec(const std::vector<int> &data);
    void frame(const Frame &f);
};

const int N = 10'000'000;                  // 1000 万个 int，约 40 MB
QByteArray ba(N * 4, 'x');
std::vector<int> v(N, 1);
Frame f;
f.samples.assign(N, 1);
// 接收方在工作线程中，记下拿到的数据的地址，和发送方的地址比较
```

```
QByteArray   emit 用时   0.34 ms，接收方拿到的是同一块内存（没有拷贝数据）
std::vector  emit 用时   5.90 ms，接收方拿到的是另一块内存（数据被拷贝了）
Frame        emit 用时   5.23 ms，接收方拿到的是另一块内存（数据被拷贝了）
Frame 的拷贝构造函数被调用了 1 次
```

- `std::vector` 和 `Frame` 都被完整地拷贝了一次，40 MB 的拷贝用了 5～6 毫秒。
- `QByteArray` 没有拷贝数据，接收方拿到的就是同一块内存。

### 3.2 隐式共享

`QByteArray`、`QString`、`QList`、`QImage`、`QPixmap` 等 Qt 的容器和数据类都使用**隐式共享**（implicit sharing，也叫写时复制，copy-on-write）：

- 拷贝一个 `QByteArray` 时，只拷贝一个指针，两个对象**共享**同一块数据，并把这块数据的引用计数加 1；
- 任何一方要**修改**数据时，如果数据是共享的，才真正复制一份，再修改自己的副本。

所以队列连接「拷贝」一个 `QByteArray`，实际只是让引用计数加 1，40 MB 的数据一个字节都没有复制。引用计数是原子操作，可以在多个线程间安全地使用。

这对跨线程的设计很有意义：**跨线程传递大块数据时，优先用 Qt 的隐式共享类型**（`QByteArray`、`QList`、`QImage`），只要发送方之后不再修改它，就没有拷贝的开销。`std::vector` 没有隐式共享，每次跨线程都是一次完整的拷贝；不想拷贝的话，可以用 `std::shared_ptr<const std::vector<int>>` 传递。

### 3.3 自定义类型

`Frame` 是自己定义的结构体，没有做任何额外的工作就能用在跨线程的信号中。Qt 6 的 moc 在生成代码时，会自动登记信号参数的类型（第一章《信号与槽》第 6.2 节中 moc 输出的 `QMetaType` 部分）。只要类型能拷贝构造，并且在 `Q_OBJECT` 类的头文件中是完整的定义，就可以直接使用。Qt 5 中还需要 `Q_DECLARE_METATYPE` 和 `qRegisterMetaType`，读老代码时会经常看到它们。

## 4. 队列会堆积

队列连接把信号变成了事件，事件在队列中排队。如果发送方产生消息的速度比接收方处理的速度快，队列就会越来越长。

一个生产者在界面线程中连续发出 100 万条 100 字节的消息，消费者在工作线程中处理每条要 10 微秒：

```cpp
QObject::connect(&s, &Sender::bytes, &consumer, [&](const QByteArray &d) {
    QThread::usleep(10);                     // 消费者处理一条要 10 微秒
    handled += d.size() > 0;
});

for (int i = 0; i < 1'000'000; ++i)
    emit s.bytes(QByteArray(100, char('a' + i % 26)));   // 每条 100 字节的新数据
```

```
发出 100 万条用了 80 ms，此时消费者处理了 5063 条，内存 +361 MB
全部处理完用了 15031 ms，内存 +361 MB
```

- 发出 100 万条只用了 80 毫秒，平均每条 80 纳秒（包括创建 `QByteArray`）。这时消费者才处理了 5000 条，剩下的 99.5 万条都在队列中。
- 队列让内存多了 361 MB，平均每条约 380 字节：100 字节的数据，加上 `QByteArray` 的头部、事件对象、参数的拷贝。
- 消费者用了 15 秒才处理完。如果消费者是界面线程，这 15 秒内用户的操作都要排在这 99.5 万个事件后面，界面同样卡死了。
- 全部处理完后，内存的数字仍然是 +361 MB。这些内存已经释放了，但内存分配器把它们留在进程中以备再用，没有还给操作系统，所以常驻内存没有下降。

这种情况在实际中很常见：网络线程每收到一个数据包就发一个信号给界面，下载速度一快，界面就忙不过来了。解决的思路是**让发送方慢下来，或者把消息合并**：

- **合并**：工作线程把数据攒在自己那里，每 50 毫秒发一次信号，一次交给界面一批；界面上的进度条，每秒更新 20 次已经足够流畅，没必要每个数据包更新一次。
- **只保留最新的**：对于状态类的消息（当前进度、当前速度），只关心最新的值，旧的值没有意义。可以用一个原子变量保存最新值，界面线程用定时器去读。
- **反压**（backpressure）：消费者处理不过来时，让生产者停下来等待，例如用 BlockingQueuedConnection，或者有界的队列。

## 5. 小结

- 连接方式决定槽在哪个线程执行：Direct 在发出信号的线程中立即执行；Queued 投递到接收者所属线程的事件队列；BlockingQueued 投递后等待执行完；默认的 Auto 在每次 `emit` 时比较「执行 `emit` 的线程」和「接收者所属的线程」，相同就 Direct，否则就 Queued。
- 跨线程时强制用 Direct，槽会在别的线程中访问对象，需要自己保证线程安全。
- 同一线程中的 Queued 可以把槽推迟到当前处理结束之后；同一线程中的 BlockingQueued 会死锁，Qt 只打印警告，不会阻止。
- 队列连接要拷贝参数。Qt 的隐式共享类型（`QByteArray`、`QString`、`QList`、`QImage`）拷贝时只增加引用计数，跨线程传递大块数据时优先使用它们；`std::vector` 会被完整拷贝。
- 发送方比接收方快时，队列会堆积，内存上涨，接收方（尤其是界面线程）被淹没。要合并消息、只保留最新值，或者让发送方等待。

## 练习

1. 在第 2.1 节的实验中，在第 1 次和第 2 次 `emit` 之间调用 `r.moveToThread(QThread::currentThread())`（注意：`moveToThread` 必须在对象当前所属的线程中调用），第 2 次 `emit` 时 Auto 会怎样决定？
2. 给第 3.1 节的 `Frame` 改用 `QList<int>` 保存样本，重新测量。然后在发送之后，发送方修改 `f.samples[0]`，接收方看到的是修改前还是修改后的值？为什么？
3. 用「合并」的方法改写第 4 节：工作线程每收到一条消息就放进自己的一个 `QList`，用一个 50 毫秒的定时器把攒下的消息一次发给界面线程。发出的信号减少到了多少条？内存峰值是多少？
4. 用 `Qt::SingleShotConnection` 实现「只在第一次收到数据时显示欢迎信息」，和自己在槽中调用 `disconnect` 相比，有什么好处？
