---
title: QtConcurrent 与线程池
date: 2026-10-28 16:00:00
updated: 2026-10-28 16:00:00
categories:
  - 客户端开发
  - 事件与多线程
tags:
  - Qt
  - C++
  - 多线程
  - 线程池
order: 4
description: 一次性的计算任务不必自己管理线程：QtConcurrent::run 把它交给线程池，QFutureWatcher 在界面线程中通知结果。把第二章那次排序 100 万个字符串搬到后台，界面的心跳最大间隔只有 21 毫秒。用 mapped 并行生成 48 张缩略图：2 个线程快 1.9 倍，12 个线程只快 3.7 倍，因为 2.3 GB 的图片数据已经把内存带宽用满了。mapped 能报告进度，也能中途取消：800 毫秒时取消，1000 项任务完成了 398 项就停了。最后是线程池最常见的误用：24 个各阻塞 1 秒的任务占满了 12 个线程，一个只要几微秒的计算排在它们后面，等了 2006 毫秒才开始执行；把阻塞的任务放进单独的线程池，它立即就执行了。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解线程池的作用；会用 `QtConcurrent::run` 在后台执行一个函数，用 `QFuture` 和 `QFutureWatcher` 在界面线程中拿到结果；会用 `QtConcurrent::mapped` / `blockingMapped` 并行处理一批数据，理解加速比为什么达不到核数；会报告进度和取消；知道不能把阻塞的任务放进全局线程池。
>
> **前置知识**：本章前三篇；C++11 专栏《条件变量与 async / future》（`std::future` 的概念）、C++17 专栏的并行算法。
>
> **实验环境**：macOS 27.0（Apple M6，12 核），Qt 6.12.0，Release 构建。用到 Qt Concurrent 模块：`find_package(Qt6 REQUIRED COMPONENTS Concurrent)`，链接 `Qt6::Concurrent`。

## 1. 线程池

上一篇的「工作对象 + `QThread`」适合长期存在、不断接收任务的工作，比如一个专门负责网络的线程。可是很多任务是**一次性**的：排序一次、解析一个文件、生成一批缩略图。为每个任务创建一个线程，用完再销毁，既麻烦又浪费：创建线程本身要几十微秒，还要自己管理退出。

**线程池**事先创建好一组线程，任务来了就交给一个空闲的线程执行，执行完线程不退出，等待下一个任务；所有线程都忙时，新任务排队等待。

Qt 有一个全局的线程池 `QThreadPool::globalInstance()`，线程数默认等于 CPU 的核数：

```
CPU 核数 12，全局线程池最多 12 个线程
```

**Qt Concurrent** 模块在线程池之上提供了一组高层的函数，不需要和线程直接打交道：

| 函数 | 作用 |
|---|---|
| `QtConcurrent::run(函数, 参数...)` | 在线程池中执行一个函数，返回 `QFuture` |
| `QtConcurrent::mapped(容器, 函数)` | 对容器中的每个元素并行地调用函数，返回结果的 `QFuture` |
| `QtConcurrent::filtered(容器, 函数)` | 并行地筛选 |
| `QtConcurrent::mappedReduced(...)` | 并行地映射，再把结果合并 |
| `blockingMapped` 等 `blocking` 开头的版本 | 同上，但阻塞到全部完成才返回 |

## 2. QtConcurrent::run 与 QFutureWatcher

### 2.1 在后台排序

第二章 2.2 篇中，对 100 万个字符串排序（`std::sort`）用了 68 毫秒。如果在界面线程中做，界面会卡 68 毫秒；数据再多，就是几秒。把它放到后台：

```cpp
#include <QFutureWatcher>
#include <QtConcurrent>

QStringList names;                       // 100 万个「联系人 i」
// ……

QFutureWatcher<QStringList> watcher;
QObject::connect(&watcher, &QFutureWatcher<QStringList>::finished, [&] {
    QStringList sorted = watcher.result();
    std::printf("%5lld ms  [%s] 排序完成，第一个是 %s，心跳最大间隔 %lld ms\n", clock.elapsed(),
                QThread::currentThread()->objectName().toUtf8().data(), qPrintable(sorted[0]), maxGap);
});

std::printf("%5lld ms  开始后台排序\n", clock.elapsed());
watcher.setFuture(QtConcurrent::run([names]() mutable {     // 按值捕获：隐式共享，不拷贝数据
    std::sort(names.begin(), names.end(), std::greater<QString>());
    return names;
}));
std::printf("%5lld ms  QtConcurrent::run 立即返回\n", clock.elapsed());
```

同时运行一个 20 毫秒的心跳：

```
  103 ms  开始后台排序
  103 ms  QtConcurrent::run 立即返回
  180 ms  [界面线程] 排序完成，第一个是 联系人 999999，心跳最大间隔 21 ms
```

- `QtConcurrent::run` 立即返回一个 **`QFuture<QStringList>`**，代表「将来会有的一个结果」，和 `std::future` 的概念一样。
- **`QFutureWatcher`** 监视一个 `QFuture`，任务完成时发出 `finished` 信号。`watcher` 属于界面线程，所以 `finished` 的槽在界面线程中执行，可以直接更新界面。
- 排序进行期间，界面线程的心跳最大间隔 21 毫秒，完全没有被卡住。

几个细节：

- **lambda 按值捕获 `names`**。如果按引用捕获，后台线程排序时，界面线程可能同时在读取或修改 `names`，就是数据竞争。按值捕获得到一份独立的副本。`QStringList` 是隐式共享的（上一篇第 3.2 节），捕获时只增加引用计数；后台线程开始排序（修改）时，才真正复制一份数据，复制发生在后台线程中，不占用界面线程的时间。
- 不要在界面线程中调用 `future.result()` 或 `future.waitForFinished()` 去等结果：它们会阻塞到任务完成，又把界面卡住了。用 `QFutureWatcher` 的信号。
- Qt 6 的 `QFuture` 也支持 `.then()` 写法：`QtConcurrent::run(...).then(&app, [](QStringList r) { ... })`，第一个参数是上下文对象，后续的函数会在它所在的线程中执行，效果和 `QFutureWatcher` 一样。

### 2.2 run 不能取消

`QtConcurrent::run` 返回的 `QFuture` 不支持取消、暂停和进度报告：函数一旦开始执行，就不能从外部打断。需要中途取消的长任务，要像上一篇第 5.2 节那样，让任务自己定期检查一个标志；或者使用 `QPromise` 版本的 `run`，在任务中调用 `promise.isCanceled()` 检查。

## 3. mapped：并行处理一批数据

### 3.1 生成缩略图

图片浏览器打开一个文件夹，要为几十张照片生成缩略图。每张照片的处理互不相关，正适合并行。用 48 张 4000×3000 的图片做实验（程序生成的渐变加噪点，代替真实的照片文件），每张缩小到 1/20：

```cpp
// 生成缩略图：缩小到 1/20（200×150），每个像素取对应 20×20 区域的平均值
QImage thumbnail(const QImage &photo)
{
    const int k = 20;
    QImage out(photo.width() / k, photo.height() / k, QImage::Format_RGB32);
    for (int y = 0; y < out.height(); ++y) {
        auto *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) {
            int r = 0, g = 0, b = 0;
            for (int dy = 0; dy < k; ++dy) {
                auto *src = reinterpret_cast<const QRgb *>(photo.constScanLine(y * k + dy)) + x * k;
                for (int dx = 0; dx < k; ++dx) {
                    r += qRed(src[dx]);
                    g += qGreen(src[dx]);
                    b += qBlue(src[dx]);
                }
            }
            dst[x] = qRgb(r / (k * k), g / (k * k), b / (k * k));
        }
    }
    return out;
}
```

这里用的是 `QImage` 而不是 `QPixmap`：上一篇说过，`QImage` 可以在任何线程中使用。

单线程处理 48 张，和用 `blockingMapped` 并行处理，分别限制线程池的线程数为 1、2、4、8、12：

```cpp
QList<QImage> a;
for (const QImage &p : photos)
    a << thumbnail(p);                                            // 单线程

QThreadPool::globalInstance()->setMaxThreadCount(threads);
QList<QImage> b = QtConcurrent::blockingMapped(photos, thumbnail);  // 并行
```

每种配置运行 5 次，取最快的一次（排除第一次访问内存等干扰）。整个程序运行三遍，结果很稳定，取其中一遍：

```
单线程   48 张:    68.9 ms
 1 个线程 48 张:    68.3 ms，加速 1.0 倍，结果相同
 2 个线程 48 张:    36.4 ms，加速 1.9 倍，结果相同
 4 个线程 48 张:    24.6 ms，加速 2.8 倍，结果相同
 8 个线程 48 张:    19.6 ms，加速 3.5 倍，结果相同
12 个线程 48 张:    18.8 ms，加速 3.7 倍，结果相同
```

`blockingMapped` 一行代码就把工作分给了线程池中的线程，结果的顺序和输入一致，和单线程的结果完全相同。

### 3.2 为什么只快了 3.7 倍

2 个线程快了 1.9 倍，接近理想的 2 倍；4 个线程只快 2.8 倍；12 个线程只有 3.7 倍，而且从 8 个线程到 12 个几乎没有变化。

算一下数据量：每张图片 4000 × 3000 × 4 字节 = 48 MB，48 张一共约 2.3 GB。12 个线程用 18.8 毫秒读完 2.3 GB，相当于每秒约 120 GB。缩小图片的计算很简单（每个像素做几次加法），时间主要花在**从内存中读取像素**上，而所有的核共用同一套内存，内存每秒能传输的数据量（内存带宽）是有上限的。线程少的时候，计算是瓶颈，加线程就快；线程多到一定程度，瓶颈变成了内存带宽，再加线程也没用。

另一个原因是这 12 个核并不一样快。用 `sysctl` 查看：

```bash
sysctl hw.perflevel0.name hw.perflevel0.physicalcpu hw.perflevel1.name hw.perflevel1.physicalcpu \
       hw.perflevel2.name hw.perflevel2.physicalcpu
```

```
hw.perflevel0.name: Super
hw.perflevel0.physicalcpu: 2
hw.perflevel1.name: Performance
hw.perflevel1.physicalcpu: 4
hw.perflevel2.name: Efficiency
hw.perflevel2.physicalcpu: 6
```

实验用的芯片有三档核心：2 个最快的、4 个性能核、6 个能效核。线程超过 6 个以后，多出来的线程只能跑在较慢的能效核上，对总时间的贡献更小了。

所以多线程的加速比取决于任务的性质：

- **计算密集**（每个数据要做大量的计算，例如图像识别、压缩、加密）：加速比接近核数；
- **内存密集**（每个数据只做很少的计算，例如复制、简单的缩放、求和）：很快被内存带宽限制；
- **任务太小**：每个任务只要几微秒时，分配任务、同步结果的开销就占了主要部分，并行反而可能更慢。`mapped` 会自动把小任务分批交给线程，减轻这个问题。

决定要不要并行、开多少个线程之前，像这样量一下。

### 3.3 进度和取消

不带 `blocking` 的 `mapped` 立即返回，配合 `QFutureWatcher`，可以在界面上显示进度，也可以取消。1000 个任务，每个耗时 20 毫秒，800 毫秒时用户点了「取消」：

```cpp
int work(int x)
{
    QThread::msleep(20);
    return x * x;
}

QFutureWatcher<int> watcher;
QObject::connect(&watcher, &QFutureWatcher<int>::progressValueChanged, [&](int v) {
    // 每 100 项打印一次，实际的程序中更新进度条
    std::printf("%5lld ms  进度 %d / %d\n", clock.elapsed(), v, watcher.progressMaximum());
});
QObject::connect(&watcher, &QFutureWatcher<int>::finished, [&] {
    std::printf("%5lld ms  finished，isCanceled = %d，已完成 %d 项\n", clock.elapsed(),
                watcher.isCanceled(), watcher.progressValue());
});
watcher.setFuture(QtConcurrent::mapped(items, work));
QTimer::singleShot(800, [&] {
    std::printf("%5lld ms  用户点了取消\n", clock.elapsed());
    watcher.cancel();
});
```

```
    0 ms  进度 0 / 1000
  260 ms  进度 121 / 1000
  490 ms  进度 240 / 1000
  712 ms  进度 350 / 1000
  801 ms  用户点了取消
  826 ms  finished，isCanceled = 1，已完成 398 项
```

- 进度大约每 0.23 秒增加 120 项，12 个线程并行，每个任务 20 毫秒，和预期一致（理想情况是每 0.2 秒 120 项）。
- 取消后 25 毫秒，`finished` 就发出了，一共完成了 398 项。正在执行的任务会执行完（每个最多 20 毫秒），还没开始的任务被丢弃。`mapped` 能取消，是因为它在开始每一个元素之前都会检查是否被取消了。

进度信号也是跨线程的队列连接，`QFutureWatcher` 会自动限制发出进度信号的频率，不会出现上一篇第 4 节那样的堆积。

## 4. 不要阻塞线程池

全局线程池只有 12 个线程，是为**计算**准备的：一个线程对应一个核，让所有的核都忙起来。如果放进去的任务大部分时间在**等待**（等网络响应、等文件读完、`sleep`），线程就被占着却不干活。

提交 24 个「等待网络响应」的任务，每个阻塞 1 秒；然后提交一个只要几微秒的计算任务，让它记下自己开始执行的时刻：

```cpp
QList<QFuture<void>> waits;
for (int i = 0; i < 24; ++i)                 // 24 个「等网络响应」的任务，每个阻塞 1 秒
    waits << QtConcurrent::run([] { QThread::sleep(1); });
std::printf("提交了 24 个阻塞任务，活动线程 %d / %d\n", pool->activeThreadCount(), pool->maxThreadCount());

std::atomic<qint64> startedAt{-1};
QtConcurrent::run([&] { startedAt = t.elapsed(); });       // 一个只要几微秒的计算，记下它开始执行的时刻
```

```
提交了 24 个阻塞任务，活动线程 12 / 12
快速任务在第 2006 ms 才开始执行
24 个阻塞任务全部完成: 2010 ms
```

12 个线程全被阻塞的任务占着，另外 12 个阻塞任务在排队，快速任务排在它们后面，**等了 2 秒才开始**。程序中所有用到全局线程池的地方（包括 Qt 自己内部用到的）都会被拖慢。

把阻塞的任务放进一个单独的线程池：

```cpp
QThreadPool ioPool;
ioPool.setMaxThreadCount(32);
for (int i = 0; i < 24; ++i)
    waits << QtConcurrent::run(&ioPool, [] { QThread::sleep(1); });   // 第一个参数指定线程池
```

```
阻塞任务放进单独的池: 快速任务在第 0 ms 开始执行
24 个阻塞任务全部完成: 1005 ms
```

快速任务立即执行了；24 个阻塞任务有 32 个线程可用，同时等待，1 秒就全部完成。

> 实验中有一个容易踩的坑：如果用 `future.waitForFinished()` 去等那个快速任务，它会在第 0 毫秒就完成。原因是 `waitForFinished()` 发现任务还在队列中没有开始时，会把它从队列中取出来，直接在**调用者的线程**中执行。这避免了一些死锁，但也会让测量结果失真，所以这里让任务自己记录开始的时刻。

更好的做法是：等待网络、文件这类 IO，根本就不需要占用一个线程去等。下一章会看到，Qt 的网络类都是**异步**的：发出请求后立即返回，数据到了由事件循环通知，一个线程就能同时处理成百上千个连接（和网络专栏《IO 多路复用》中 epoll 的思路一样）。

## 5. 怎样选择

到这里，第三章介绍了在 Qt 中使用多线程的几种方式，按任务的性质选择：

| 任务 | 推荐 |
|---|---|
| 一次性的计算（排序、解析、压缩） | `QtConcurrent::run` + `QFutureWatcher` |
| 一批相互独立的数据（缩略图、批量转换） | `QtConcurrent::mapped` / `filtered`，可显示进度和取消 |
| 长期运行、不断接收任务、需要自己的事件循环（专门的网络线程、设备通信） | 工作对象 + `moveToThread` |
| 一段从头跑到尾的循环 | `QThread::create` 或继承 `QThread` 重写 `run()` |
| 等待网络、文件等 IO | 优先用异步的 API（第 4 章）；必须阻塞时，用单独的线程池 |

无论用哪一种，规则都一样：**界面只在界面线程中操作，工作线程的结果通过信号（或 `QFutureWatcher`）送回界面线程。**

## 6. 小结

- 线程池事先创建一组线程反复使用。全局线程池的线程数默认等于核数，供计算任务使用。
- `QtConcurrent::run` 在线程池中执行一个函数，返回 `QFuture`；用 `QFutureWatcher` 的 `finished` 信号（或 `QFuture::then`）在界面线程中拿结果，不要在界面线程中 `waitForFinished`。传给后台的数据按值捕获，避免数据竞争。
- `mapped` 并行处理一批数据，结果的顺序和输入一致，支持进度和取消。
- 并行的加速比受限于任务的性质：内存密集的任务很快被内存带宽限制，实验中 12 个线程只快了 3.7 倍。
- 不要把阻塞等待的任务放进全局线程池，它们会占满线程，让所有计算任务排队；必须阻塞时用单独的线程池，最好改用异步的 API。

第三章到这里结束。前三章的程序都是在本机上运行的，下一章让客户端连上网络：用 `QTcpSocket` 连接网络专栏写的服务器，处理 TCP 的粘包和断线重连，用 `QNetworkAccessManager` 发 HTTP 请求，用 SQLite 保存本地数据。

## 练习

1. 把第 3.1 节的 `thumbnail` 改成计算密集的版本：每个输出像素额外做 1000 次浮点运算。重新测量 1～12 个线程的加速比，和原来的结果比较。
2. 用 `QtConcurrent::filtered` 在第二章 2.2 篇的 100 万个联系人中，并行地找出所有包含「7」的名字。和代理模型的过滤（220 毫秒）相比快了多少？
3. 用 `QPromise` 版本的 `QtConcurrent::run` 写一个可以取消、能报告进度的长任务（提示：任务函数的第一个参数是 `QPromise<T> &`）。
4. 在第 4 节的实验中，把阻塞任务的数量改成 11 个和 12 个，快速任务分别在什么时候开始执行？为什么？
