---
title: 对象树与内存管理
date: 2026-10-27 12:00:00
updated: 2026-10-27 12:00:00
categories:
  - 客户端开发
  - Qt入门
tags:
  - Qt
  - C++
  - 内存管理
  - AddressSanitizer
order: 4
description: 前几篇的控件都是 new 出来的，从来没有 delete，靠的是 Qt 的对象树：父对象析构时删除所有子对象。本文先观察一棵树的删除顺序，再用 AddressSanitizer 重现 Qt 中最常见的四种内存错误：先声明按钮、后声明窗口，程序在退出时 abort，ASan 指出 Qt 在 delete 一个栈上的变量；忘了给 1000 个标签设父对象，窗口关掉后它们全都还活着，macOS 的 leaks 工具却一个也没报出来；lambda 捕获的对象被删除后，定时器再触发一次，程序崩溃，退出码 139；在槽中直接 delete 发出信号的对象，不开 ASan 时程序「正常」结束，只是 URL 悄悄变成了空字符串。最后用 deleteLater、QPointer 和上下文对象逐一修好。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解 Qt 的对象树：父对象拥有子对象，析构时删除它们；知道 `new` 出来的对象什么时候需要 `delete`、什么时候不需要；会避免栈对象与对象树之间的重复删除；会用上下文对象、`deleteLater`、`QPointer` 避免悬空指针；会用 AddressSanitizer 检查 Qt 程序的内存错误。
>
> **前置知识**：本章前三篇；C++ 的 `new` / `delete`、析构函数和析构顺序（C++基础专栏《动态内存：new 与 delete》《构造函数与析构函数》）；智能指针（C++11 专栏《智能指针》）；AddressSanitizer（《开发环境与编程工具》第 7 节）。
>
> **实验环境**：同上一篇，macOS 27.0，Qt 6.12.0，Apple Clang 21。用 ASan 的实验加上了 `-fsanitize=address -g`。

## 1. 对象树

现代 C++ 的建议是：尽量不写裸的 `new`，用智能指针管理堆上的对象。可是 Qt 的代码中到处是 `new QLabel(...)`、`new QPushButton(...)`，却几乎看不到 `delete`。这些对象是谁释放的？

每个 `QObject` 都可以有一个**父对象**（parent）和任意多个**子对象**（children）。构造时传入父对象，或者之后调用 `setParent()`，这个对象就成为父对象的子对象。所有的对象由此组成一棵**对象树**，规则只有一条：

> **父对象析构时，会先删除它所有的子对象。**

所以只要一个对象有父对象，就不用自己 `delete` 它，父对象会负责。界面中，所有控件最终都挂在某个窗口下面，删除窗口，整个界面就都释放了。

### 1.1 观察删除的顺序

写一个析构时打印名字的类：

```cpp
// 析构时打印自己的名字
class Node : public QObject
{
    Q_OBJECT
public:
    Node(const char *name, QObject *parent = nullptr) : QObject(parent)
    {
        setObjectName(name);
    }
    ~Node() override { std::printf("  ~Node %s\n", qPrintable(objectName())); }
};
```

`objectName` 是 `QObject` 自带的一个名字属性，调试时很有用，也可以用 `findChild` 按名字查找子对象。`qPrintable` 把 `QString` 转成 `const char *`，方便传给 `printf`。

搭一棵树：

```cpp
Node *root = new Node("root");
Node *a = new Node("a", root);
new Node("a1", a);
new Node("a2", a);
Node *b = new Node("b", root);
new Node("b1", b);

std::printf("root 有 %lld 个子对象，a 的父对象是 %s\n",
            (long long)root->children().size(), qPrintable(a->parent()->objectName()));
root->dumpObjectTree();
```

```
root 有 2 个子对象，a 的父对象是 root
Node::root
    Node::a
        Node::a1
        Node::a2
    Node::b
        Node::b1
```

`dumpObjectTree()` 把整棵树打印出来（格式是「类名::对象名」），调试界面时可以用它查看控件的层次。

接着做三件事：直接删除一个子对象、把 `b` 从树中拿出来、删除根：

```cpp
std::printf("delete a2:\n");
delete root->findChild<Node *>("a2");
std::printf("a 还剩 %lld 个子对象\n", (long long)a->children().size());

std::printf("b->setParent(nullptr):\n");
b->setParent(nullptr);          // b 离开了树，root 不再负责删除它

std::printf("delete root:\n");
delete root;
std::printf("delete b:\n");
delete b;
```

```
delete a2:
  ~Node a2
a 还剩 1 个子对象
b->setParent(nullptr):
delete root:
  ~Node root
  ~Node a
  ~Node a1
delete b:
  ~Node b
  ~Node b1
```

从中可以看出：

- **可以直接 `delete` 一个子对象**：它的析构函数会通知父对象，把自己从父对象的子对象列表中移除，所以 `a` 只剩一个子对象，之后删除 `a` 时也不会再删一次 `a2`。
- **`setParent(nullptr)` 让对象离开树**：`root` 析构时没有删除 `b`，`b` 又变成需要自己负责的对象了。
- **先打印父对象，再打印子对象**：`delete root` 时先调用 `~Node`（打印 root），然后执行基类的 `~QObject`，子对象是在 `~QObject` 中被删除的。所以在派生类的析构函数中，子对象都还活着，可以放心访问。

### 1.2 什么时候需要 delete

| 情况 | 谁负责释放 |
|---|---|
| `new` 时传入了父对象，或放进了布局（上一篇 3.4 节） | 父对象 |
| 主窗口、独立的对话框等**顶层窗口** | 自己：通常直接在栈上创建（`QWidget window;`），或用智能指针 |
| 没有父对象的 `QObject`（例如一个独立的网络连接对象） | 自己：栈上创建、`std::unique_ptr`，或者事后给它设父对象 |
| 设置了 `Qt::WA_DeleteOnClose` 属性的窗口 | 关闭时自动删除自己 |

**注意不要把对象树和 `std::unique_ptr`、`std::shared_ptr` 混用在同一个对象上**：一个有父对象的控件，如果又被智能指针管着，就会被删除两次。一个对象只能有一个「主人」。

## 2. 栈上的对象与析构顺序

对象树的规则是「父对象 `delete` 子对象」。如果子对象根本不是 `new` 出来的呢？

```cpp
{
    QPushButton button("确定");     // 先构造子对象
    QWidget window;                // 后构造父对象
    button.setParent(&window);
    std::printf("bad: 离开作用域\n");
}
std::printf("正常结束\n");
```

```
bad: 离开作用域
```

```bash
echo $?
```

```
134
```

程序在离开作用域时 abort 了，「正常结束」没有打印。用 ASan 构建，再运行一次：

```
==50188==ERROR: AddressSanitizer: attempting free on address which was not malloc()-ed: 0x00016f0522b0 in thread T0
    #0 0x000102e00558 in _ZdlPv+0x74 (libclang_rt.asan_osx_dynamic.dylib:arm64e+0x50558)
    #1 0x000101f821ec in QObjectPrivate::deleteChildren()+0x50 (QtCore:arm64+0xc61ec)
    #2 0x00010184967c in QWidget::~QWidget()+0x390 (QtWidgets:arm64+0x3d67c)
    #3 0x000100db2a58 in main stackorder.cpp:15
Address 0x00016f0522b0 is located in stack of thread T0 at offset 80 in frame
    #0 0x000100db26c0 in main stackorder.cpp:8
  This frame has 10 object(s):
    [32, 36) 'argc.addr'
    [48, 64) 'app' (line 9)
    [80, 120) 'button' (line 11) <== Memory access at offset 80 is inside this variable
```

ASan 说得很清楚：`QWidget::~QWidget()` 中的 `deleteChildren()` 对 `button` 调用了 `operator delete`（`_ZdlPv`），而 `button` 是 `main` 栈上的变量，不是 `new` 出来的。

原因是 C++ 的规则：**局部变量按构造的相反顺序析构**。`window` 后构造，先析构；它析构时按对象树的规则 `delete` 了子对象 `button`，可 `button` 在栈上，`delete` 一个栈上的地址就是未定义行为。即使侥幸没有崩溃，之后 `button` 离开作用域时还会再析构一次。

把顺序反过来就没有问题：

```cpp
{
    QWidget window;                // 先构造父对象
    QPushButton button("确定", &window);
    std::printf("good: 离开作用域\n");
}
std::printf("正常结束\n");
```

```
good: 离开作用域
正常结束
```

`button` 后构造、先析构，析构时把自己从 `window` 的子对象列表中移除（1.1 节的第一条）；`window` 析构时已经没有子对象了。

规则可以简单地记成：**除了顶层窗口，控件都用 `new` 创建并给它父对象**。前面几篇的代码都是这样写的。

## 3. 忘了给父对象：泄漏

反过来，`new` 出来的对象没有父对象、也没有 `delete`，就泄漏了。在一个窗口的函数中创建 1000 个标签，一次给父对象，一次不给：

```cpp
void makeLabels(QWidget *window, bool withParent)
{
    for (int i = 0; i < 1000; ++i) {
        QLabel *label = withParent ? new QLabel("x", window) : new QLabel("x");
        (void)label;            // 没有父对象，也没有 delete
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    {
        QWidget window;
        makeLabels(&window, argc > 1);
        std::printf("创建后，还活着的控件: %lld\n", (long long)QApplication::allWidgets().size());
    }
    std::printf("window 析构后，还活着的控件: %lld，顶层窗口: %lld\n",
                (long long)QApplication::allWidgets().size(),
                (long long)QApplication::topLevelWidgets().size());
    return 0;
}
```

`QApplication::allWidgets()` 返回程序中所有还活着的控件。

```
# 没有父对象
创建后，还活着的控件: 1001
window 析构后，还活着的控件: 1000，顶层窗口: 1000

# 有父对象
创建后，还活着的控件: 1001
window 析构后，还活着的控件: 0，顶层窗口: 0
```

没有父对象时，`window` 析构了，1000 个标签还在；而且每一个都是「顶层窗口」，只是没有调用 `show()` 才没有显示出来。如果这个函数是一个对话框，每打开一次就多 1000 个控件，程序的内存会越用越多。

用 macOS 自带的泄漏检查工具 `leaks` 检查一下（Linux 上可以用 Valgrind 或者 LeakSanitizer）：

```bash
MallocStackLogging=1 leaks --atExit -- ./leak          # 没有父对象
MallocStackLogging=1 leaks --atExit -- ./leak parent   # 有父对象
```

```
Process 50733: 24632 nodes malloced for 4200 KB
Process 50733: 415 leaks for 26416 total leaked bytes.

Process 50739: 19582 nodes malloced for 2686 KB
Process 50739: 414 leaks for 26384 total leaked bytes.
```

两次的泄漏数几乎一样（这 414 个来自 Qt 和系统库本身，空程序也有，和实验无关），`leaks` **没有发现**这 1000 个标签。但程序结束时还在使用的内存多了约 1.5 MB（4200 KB 对 2686 KB）。

原因是泄漏检查工具找的是「已经没有任何指针指向的内存」，而 Qt 把每个控件都登记在 `QApplication` 的一个全局列表中（`allWidgets()` 就是读的它），这些标签仍然「可以找到」，所以不算泄漏。这类「对象还被某处引用着、但再也不会用到」的泄漏，靠工具很难发现，要靠代码习惯：**`new` 一个 `QObject` 时，马上想清楚它的父对象是谁**。

## 4. 悬空指针

对象树解决了「谁来删除」，但删除之后，别处如果还保存着指向它的指针，就是悬空指针。Qt 程序中最常见的有两种。

### 4.1 lambda 捕获了被删除的对象

一个定时器每 100 毫秒更新一次界面 `view`，250 毫秒时 `view` 被删除了（比如用户关掉了这个页面）：

```cpp
QTimer timer;
Node *view = new Node("view");

QObject::connect(&timer, &QTimer::timeout, [view] {
    std::printf("更新 %s\n", qPrintable(view->objectName()));
});
timer.start(100);
QTimer::singleShot(250, [view] { std::printf("删除 view\n"); delete view; });
QTimer::singleShot(500, &app, &QCoreApplication::quit);
return app.exec();
```

```
更新 view
更新 view
删除 view
  ~Node view
```

```bash
echo $?
```

```
139
```

第三次 `timeout`（300 毫秒）时，lambda 访问了已经删除的 `view`，程序崩溃（`SIGSEGV`）。上一篇说过「接收者被删除时，连接自动断开」，可这里的连接只有发送者 `timer`，没有接收者，Qt 不知道这个 lambda 依赖 `view`。用 ASan 运行，能看到完整的经过：

```
==50231==ERROR: AddressSanitizer: heap-use-after-free on address 0x60300000f9a0 ...
    #10 0x0001009c89d0 in main::$_1::operator()() const dangling.cpp
freed by thread T0 here:
    #7 0x0001009ca9cc in main::$_2::operator()() const dangling.cpp
previously allocated by thread T0 here:
    #7 0x0001009c68dc in main dangling.cpp
```

第一个 lambda（`$_1`，定时更新）访问了被第二个 lambda（`$_2`，删除 view）释放的内存。

修复方法就是上一篇提到的**上下文对象**：把 `view` 作为 `connect` 的第三个参数。

```cpp
QObject::connect(&timer, &QTimer::timeout, view, [view] {   // view 作为上下文
    std::printf("更新 %s\n", qPrintable(view->objectName()));
});
```

```
更新 view
更新 view
删除 view
  ~Node view
```

程序正常结束。`view` 析构时，以它为上下文的连接被自动断开，lambda 不会再被调用。**连接 lambda 时，lambda 里用到了哪个 `QObject`，就把它作为上下文传进去**，这条习惯能避免大部分悬空指针。

### 4.2 在槽中删除发出信号的对象

一个下载对象在完成时发出 `finished` 信号。收到信号的一方觉得下载已经结束了，顺手把它删掉：

```cpp
class Download : public QObject
{
    Q_OBJECT
public:
    explicit Download(const char *url) : m_url(url) {}
    ~Download() override { std::printf("  ~Download\n"); }
    void finish()
    {
        emit finished();
        std::printf("  %s 下载完成，通知完毕\n", m_url.c_str());   // emit 之后还用到了成员
    }
signals:
    void finished();
private:
    std::string m_url;
};

// main 中
auto *d = new Download("http://example.com/a-file-with-a-long-name.zip");
QObject::connect(d, &Download::finished, [d] {
    std::printf("  收到 finished，删除下载对象\n");
    delete d;
});
std::printf("调用 finish()\n");
d->finish();
std::printf("finish() 返回，进入事件循环\n");
```

不用 ASan，直接运行：

```
调用 finish()
  收到 finished，删除下载对象
  ~Download
   下载完成，通知完毕
finish() 返回，进入事件循环
事件循环结束
```

程序「正常」结束了，退出码 0。只有仔细看才会发现，第四行的 URL 不见了。

上一篇讲过，`emit` 就是函数调用：

```
d->finish()
├── emit finished()
│   └── lambda：delete d          ← d 在这里被删除了
└── printf(m_url)                 ← 回到 finish()，this 已经是悬空指针
```

`emit` 返回时，`finish()` 所属的对象已经不存在了，后面再访问 `m_url` 读的是已经释放的内存。这次读到的恰好是空字符串，换一个环境，可能是乱码，也可能崩溃。这种「看起来正常」的错误最危险。ASan 一下子就指出了问题：

```
==50709==ERROR: AddressSanitizer: heap-use-after-free on address 0x60400000b7f7 ...
READ of size 1 at 0x60400000b7f7 thread T0
    #4 0x000102aeed1c in Download::finish() selfdelete.cpp
    #5 0x000102aee9b0 in main selfdelete.cpp
freed by thread T0 here:
    #1 0x000102aef4a0 in Download::~Download() selfdelete.cpp
    #2 0x000102af0468 in main::$_0::operator()() const selfdelete.cpp
    #10 0x000102aeed10 in Download::finish() selfdelete.cpp
```

释放发生在 `finish()` 之内（调用链中 `finish()` 在 `~Download` 的下面），读取发生在 `finish()` 之后的代码中。

发出信号的对象，在 `emit` 返回之后很可能还要做事；Qt 自己的类也一样，例如网络连接在发出信号后还要更新内部状态。所以**不要在槽中直接 `delete` 发送者**，而是用 **`deleteLater()`**：

```cpp
QObject::connect(d, &Download::finished, [d] {
    std::printf("  收到 finished，删除下载对象\n");
    d->deleteLater();
});
```

```
调用 finish()
  收到 finished，删除下载对象
  http://example.com/a-file-with-a-long-name.zip 下载完成，通知完毕
finish() 返回，进入事件循环
  ~Download
事件循环结束
```

`deleteLater()` 不立即删除对象，而是往事件队列中放一个「删除」事件（`QEvent::DeferredDelete`）。等当前的函数调用全部返回、回到事件循环时，才真正 `delete`。输出中 `~Download` 出现在「进入事件循环」之后，URL 也完整地打印出来了。这又一次用到了第 1.1 篇的事件循环：**事件循环是一个安全点，这时没有任何对象的成员函数正在执行**。

> `deleteLater()` 依赖事件循环。没有运行事件循环的线程中调用它，对象永远不会被删除。

## 5. 判断对象是否还活着：QPointer

有时候必须保存一个指向别的对象的指针，但这个对象可能在任何时候被别人删除，例如一个可能被用户关掉的对话框。普通指针无法知道对象是否还在。Qt 提供了 **`QPointer<T>`**：对象被删除时，它会自动变成空指针。

```cpp
Node *raw = new Node("dialog");
QPointer<Node> guarded = raw;
QObject::connect(raw, &QObject::destroyed, [](QObject *obj) {
    std::printf("  destroyed 信号，此时 objectName = %s\n", qPrintable(obj->objectName()));
});

std::printf("删除之前: guarded %s\n", guarded ? "有效" : "为空");
delete raw;
std::printf("删除之后: guarded %s，raw 仍然是 %p\n", guarded ? "有效" : "为空", (void *)raw);
```

```
删除之前: guarded 有效
  ~Node dialog
  destroyed 信号，此时 objectName = dialog
删除之后: guarded 为空，raw 仍然是 0x1051088f0
```

- `QPointer` 在 `delete` 之后变成了空，`raw` 仍然指向原来的地址，看不出对象已经没了。
- 每个 `QObject` 析构时都会发出 **`destroyed`** 信号，需要在对象消失时做点什么（例如从列表中移除它）时可以连接它。`QPointer` 变空、与它有关的连接断开，也都是在 `~QObject` 中完成的。`destroyed` 信号是在 `~QObject` 中发出的，派生类的部分（这里是 `~Node`）已经析构完了，所以在 `destroyed` 的槽中只能使用 `QObject` 本身的功能，例如 `objectName`，不能把 `obj` 转回 `Node *` 使用。

使用时，每次用之前检查一下：

```cpp
if (guarded)
    guarded->doSomething();
```

`QPointer` 只适用于 `QObject` 及其派生类。它不拥有对象，不会延长对象的生命周期，它和 `std::weak_ptr` 的作用相似，但不需要对象由 `shared_ptr` 管理。

## 6. 小结

- `QObject` 组成对象树：父对象析构时删除所有子对象；子对象被单独删除时，会自动从父对象中移除。有父对象的对象不需要自己 `delete`。
- 子对象在 `~QObject` 中删除，所以派生类的析构函数中子对象还活着。
- 除了顶层窗口，控件都用 `new` 创建并指定父对象。栈上的对象要注意声明顺序：父对象必须先声明，否则会被 `delete` 一个栈上的地址。
- `new` 出来没有父对象的控件会泄漏，而且泄漏检查工具不一定发现得了。
- lambda 中用到哪个对象，就把它作为 `connect` 的上下文对象，对象删除时连接自动断开。
- 不要在槽中 `delete` 发出信号的对象，用 `deleteLater()`，让事件循环在安全的时候删除它。
- 用 `QPointer` 保存可能被别人删除的对象，删除后它自动变成空。
- Qt 程序的内存错误常常「看起来正常」，开发时多用 AddressSanitizer 跑一跑。

第一章到这里就结束了：会搭建 Qt 项目，会用控件和布局搭界面，会用信号与槽让对象通信，也知道这些对象由谁来释放。下一章深入界面开发：常用控件和对话框、能高效显示大量数据的 Model/View、样式表和深色模式，以及自己画一个控件。

## 练习

1. 在 1.1 节的树中，把 `delete root` 换成 `root->deleteLater()`，程序中没有事件循环，会打印哪些析构？加上 `app.exec()` 和一个让它退出的定时器后呢？
2. 写一个对话框类，在构造函数中 `setAttribute(Qt::WA_DeleteOnClose)`，用 `new` 创建并 `show()`。用 `QPointer` 保存它，关闭对话框后检查 `QPointer` 是否为空。
3. 给 1.1 节的 `Node` 加一个子对象成员 `Node child{"member", this};`（直接作为成员，而不是 `new` 出来），删除这个 `Node` 时会发生什么？用 ASan 验证你的判断。
4. 用 `std::unique_ptr<QWidget>` 管理一个顶层窗口，再用 `new` 往里面加几个子控件，这样写有问题吗？如果把其中一个子控件也交给 `unique_ptr` 管理呢？
