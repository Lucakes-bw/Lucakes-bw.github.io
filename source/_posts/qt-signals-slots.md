---
title: 信号与槽
date: 2026-10-27 11:00:00
updated: 2026-10-27 11:00:00
categories:
  - 客户端开发
  - Qt入门
tags:
  - Qt
  - C++
  - 信号与槽
  - moc
order: 3
description: 信号与槽是 Qt 对象之间通信的方式：一个对象发出信号，连接到它的槽被调用，双方互不知道对方是谁。本文写一个能发出信号的计数器，把两个计数器双向连接起来同步数值；去掉「值没变就返回」这一行，两个对象来回调用了约 13000 次，栈溢出崩溃，退出码 139。Qt 4 的字符串写法中把信号名拼错，编译能通过，运行时 connect 返回 false；Qt 5 起的函数指针写法拼错，直接编译失败。打开 moc 生成的 128 行代码，可以看到信号其实就是一个普通的成员函数，函数体只有一行 QMetaObject::activate。最后实测开销：直接调用不到 1 纳秒，发出一个没有连接的信号约 2 纳秒，连接一个槽约 8 纳秒，连接 10 个槽约 33 纳秒。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解信号与槽解决什么问题；会定义带信号和槽的类（`Q_OBJECT`、`signals`、`slots`、`emit`）；会用 `connect` 把信号连接到成员函数或 lambda，会断开连接；理解在同一线程中发出信号时，槽是立即、按连接顺序同步调用的；知道函数指针写法比字符串写法好在哪里；知道 moc 生成了什么代码，信号与槽的开销有多大。
>
> **前置知识**：前两篇；C++ 的成员函数指针、lambda（C++11 专栏《Lambda 表达式》）、`std::function`（《std::function 与 std::bind》）。
>
> **实验环境**：同上一篇，macOS 27.0（Apple M6），Qt 6.12.0，Release 构建（`-O3`）。本篇的程序都不需要窗口，只链接 Qt Core，用 `QCoreApplication`。

## 1. 为什么需要信号与槽

界面中的对象之间要互相通知：按钮被点击了，要通知窗口；输入框的内容变了，要更新「登录」按钮是否可用；网络收到了一条消息，要显示在列表里。

最直接的办法是让按钮保存一个窗口的指针，被点击时调用 `window->onButtonClicked()`。问题是按钮就和这个窗口绑死了：`QPushButton` 是 Qt 写的，它不可能知道你的窗口类叫什么。

C 语言中常用**回调函数**：按钮保存一个函数指针，被点击时调用它。C++ 中可以用 `std::function`。这能解决问题，但还不够：

- 一个按钮的点击可能要通知好几个对象，要保存一个回调的列表；
- 接收通知的对象被删除了，按钮还拿着它的回调，再调用就是访问已经释放的内存；
- 接收者在另一个线程中时，回调应该在那个线程中执行，而不是在发出通知的线程中（第 3 章）。

Qt 的**信号与槽**（signals and slots）把这些都处理好了：

- **信号**（signal）：对象在某件事发生时发出的通知，例如 `clicked()`、`textChanged(const QString &)`。发出信号的对象不知道、也不关心谁在接收。
- **槽**（slot）：接收信号时被调用的函数。可以是成员函数，也可以是 lambda 或普通函数。
- **连接**（connect）：把一个信号和一个槽连接起来。一个信号可以连接多个槽，多个信号也可以连接同一个槽。接收者被删除时，连接自动断开。

## 2. 定义自己的信号和槽

写一个计数器类 `Counter`：保存一个整数，数值变化时发出信号 `valueChanged`。

`counter.h`：

```cpp
#pragma once
#include <QObject>

class Counter : public QObject
{
    Q_OBJECT

public:
    explicit Counter(QObject *parent = nullptr) : QObject(parent) {}
    int value() const { return m_value; }

public slots:
    void setValue(int value);

signals:
    void valueChanged(int newValue);

private:
    int m_value = 0;
};
```

`counter.cpp`：

```cpp
#include "counter.h"

void Counter::setValue(int value)
{
    if (value == m_value)
        return;             // 值没变就不发信号，防止互相连接时无限循环
    m_value = value;
    emit valueChanged(value);
}
```

要使用信号与槽，类必须满足三个条件：

1. **继承自 `QObject`**（直接或间接，所有的控件都是 `QObject`）。
2. **类的开头写 `Q_OBJECT` 宏**。
3. **类的声明放在头文件中**，并加入构建（`qt_add_executable(counter counter.h counter.cpp main.cpp)`），这样 AUTOMOC 才会对它运行 moc。

几个新的关键字：

- `signals:` 下面声明信号。**信号只有声明，没有函数体**，不需要（也不能）自己实现，函数体由 moc 生成（第 5 节）。信号的返回值总是 `void`。
- `public slots:` 下面声明槽。槽就是普通的成员函数，要自己实现，也可以像普通函数一样直接调用。在 Qt 5 之后的写法中，任何成员函数都可以当作槽，`slots` 只是一个标记，让代码更清楚。
- `emit valueChanged(value);` 发出信号。`emit` 是一个空的宏，写不写都一样，它的作用是提醒读代码的人：这里发出了一个信号，可能会调用到别处的代码。

`setValue` 中「值没变就返回」这一行很重要，第 4 节会看到去掉它的后果。Qt 自己的控件也都遵守这个约定：例如 `QLineEdit::setText()` 传入和当前相同的文字时，不会发出 `textChanged`。

## 3. 连接

### 3.1 基本用法

```cpp
#include <QCoreApplication>
#include <cstdio>
#include "counter.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    Counter a, b;

    // 1. a 变了，b 跟着变
    QObject::connect(&a, &Counter::valueChanged, &b, &Counter::setValue);
    // 2. 一个信号可以连接多个槽，也可以连接 lambda
    QObject::connect(&a, &Counter::valueChanged, [](int v) {
        std::printf("  lambda 收到 %d\n", v);
    });
    QObject::connect(&b, &Counter::valueChanged, [](int v) {
        std::printf("  b 变成了 %d\n", v);
    });

    std::printf("a.setValue(12)\n");
    a.setValue(12);
    std::printf("a=%d b=%d\n", a.value(), b.value());

    std::printf("b.setValue(48)\n");
    b.setValue(48);
    std::printf("a=%d b=%d   （连接是单向的）\n", a.value(), b.value());
    // ...
}
```

`connect` 的四个参数是：发送者、信号、接收者、槽。信号和槽都用**成员函数指针**表示：`&Counter::valueChanged`。槽是 lambda 时，可以省略接收者。

```
a.setValue(12)
  b 变成了 12
  lambda 收到 12
a=12 b=12
b.setValue(48)
  b 变成了 48
a=12 b=48   （连接是单向的）
```

注意第一段输出的顺序。`a` 的 `valueChanged` 连接了两个槽，先连接的是 `b.setValue`，后连接的是 lambda。`emit` 时：

```
a.setValue(12)
└── emit a.valueChanged(12)
    ├── 槽 1：b.setValue(12)
    │   └── emit b.valueChanged(12)
    │       └── lambda：打印「b 变成了 12」
    └── 槽 2：lambda：打印「lambda 收到 12」
```

由此可以看出信号与槽在**同一个线程**中的两条规则：

- **`emit` 就是函数调用**：所有连接的槽在 `emit` 这一行**立即同步**执行完，`emit` 才返回。槽中又发出的信号，同样立即处理，形成嵌套调用。它和上一篇的事件不同，不经过事件队列。
- **多个槽按连接的顺序调用**。

对象在不同的线程中时，情况不一样：槽会被放进接收者所在线程的事件队列，由那个线程的事件循环调用（第 3 章）。

### 3.2 双向连接与断开

再加一条反方向的连接，`a` 和 `b` 就双向同步了：

```cpp
    QObject::connect(&b, &Counter::valueChanged, &a, &Counter::setValue);
    std::printf("b.setValue(100)\n");
    b.setValue(100);
    std::printf("a=%d b=%d\n", a.value(), b.value());
```

```
b.setValue(100)
  b 变成了 100
  lambda 收到 100
a=100 b=100
```

`b.setValue(100)` 发出信号，调用 `a.setValue(100)`；`a` 又发出信号，调用 `b.setValue(100)`。这时 `b` 的值已经是 100，「值没变就返回」，循环到此结束。

`connect` 返回一个 `QMetaObject::Connection`，可以用它断开这条连接：

```cpp
    QMetaObject::Connection c = QObject::connect(&a, &Counter::valueChanged, [](int) {
        std::printf("  临时连接\n");
    });
    a.setValue(1);
    QObject::disconnect(c);
    std::printf("断开之后:\n");
    a.setValue(2);
```

```
  b 变成了 1
  lambda 收到 1
  临时连接
断开之后:
  b 变成了 2
  lambda 收到 2
```

还有一个更常用的「断开」方式是删除对象：发送者或接收者被删除时，与它有关的连接都会自动断开。连接 lambda 时，可以在信号后面多传一个**上下文对象**：

```cpp
QObject::connect(&a, &Counter::valueChanged, &window, [&window](int v) { ... });
```

`window` 被删除后，这个 lambda 就不会再被调用。如果 lambda 中用到了某个对象，就应该把这个对象作为上下文传进去，否则对象删除后再发信号，lambda 访问的就是悬空指针。下一篇讲对象的生命周期时会重现这个错误。

### 3.3 信号连接信号

`connect` 的第四个参数也可以是另一个信号，表示「这个信号发出时，接着发出那个信号」，常用来把内部控件的信号转发出去：

```cpp
// LoginDialog 内部有一个按钮，外面的人只需要知道「用户点了登录」
connect(okButton, &QPushButton::clicked, this, &LoginDialog::loginRequested);
```

## 4. 无限循环

把 `setValue` 中「值没变就返回」的两行去掉，再把 `a`、`b` 双向连接：

```cpp
int depth = 0;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    Counter a, b;
    QObject::connect(&a, &Counter::valueChanged, [](int) {
        if (++depth % 1000 == 0)
            std::fprintf(stderr, "已经来回了 %d 次\n", depth);
    });
    QObject::connect(&a, &Counter::valueChanged, &b, &Counter::setValue);
    QObject::connect(&b, &Counter::valueChanged, &a, &Counter::setValue);
    a.setValue(1);      // setValue 中去掉了「值没变就返回」的检查
    std::printf("不会执行到这里\n");
    return 0;
}
```

计数的 lambda 必须第一个连接：`emit` 按顺序调用槽，如果它排在 `b.setValue` 后面，前面的槽永远不返回，它一次也不会执行。

```
已经来回了 1000 次
已经来回了 2000 次
...
已经来回了 13000 次
```

```bash
echo $?
```

```
139
```

运行三次，都在第 13000 次之后崩溃。139 = 128 + 11，进程被 `SIGSEGV` 终止：`a.setValue` → `emit` → `b.setValue` → `emit` → `a.setValue` → ……每来回一次，调用栈就加深几层，约 13000 次后用完了 8 MB 的栈（`ulimit -s` 是 8176 KB），平均每来回一次用掉约 600 字节。这正是第 3.1 节说的「`emit` 就是函数调用」的直接后果。

实际开发中这种循环往往不那么明显，例如滑块和数字输入框互相同步、模型和界面互相更新。只要两个对象之间有双向连接，就要确认「值没变时不发信号」。

## 5. 函数指针写法与字符串写法

Qt 4 和很多老代码中，`connect` 是这样写的：

```cpp
connect(&a, SIGNAL(valueChanged(int)), &b, SLOT(setValue(int)));
```

`SIGNAL` 和 `SLOT` 两个宏把参数变成**字符串** `"2valueChanged(int)"` 和 `"1setValue(int)"`，运行时再按名字去查找。故意拼错两处：

```cpp
bool ok1 = QObject::connect(&a, SIGNAL(valueChanged(int)), &b, SLOT(setValue(int)));
bool ok2 = QObject::connect(&a, SIGNAL(valueChange(int)), &b, SLOT(setValue(int)));
bool ok3 = QObject::connect(&a, SIGNAL(valueChanged(int)), &b, SLOT(setValue(QString)));
std::printf("ok1=%d ok2=%d ok3=%d\n", ok1, ok2, ok3);
```

编译没有任何警告。运行：

```
qt.core.qobject.connect: QObject::connect: No such signal Counter::valueChange(int)
qt.core.qobject.connect: QObject::connect: No such slot Counter::setValue(QString)
ok1=1 ok2=0 ok3=0
```

只有运行时打印一行警告，`connect` 返回 `false`，程序照常运行，只是这两条连接不存在。在一个大的界面程序中，这行警告很容易被淹没，表现出来就是「点了按钮没反应」。

换成函数指针写法，同样拼错：

```cpp
QObject::connect(&a, &Counter::valueChange, &b, &Counter::setValue);
QObject::connect(&a, &Counter::valueChanged, &o, [](QString s) {});
```

```
typo.cpp:5:27: error: no member named 'valueChange' in 'Counter'; did you mean '::Counter::valueChanged'?
.../qobject.h:247:170: error: no type named 'type' in 'QtPrivate::FunctorReturnType<(lambda at typo.cpp:9:54), QtPrivate::List<>>'
```

第一处，编译器直接指出没有这个成员，还给出了正确的名字。第二处，信号的参数是 `int`，不能传给要 `QString` 的 lambda，同样编译失败（错误信息来自 Qt 的模板，不太好读，但至少它拦住了错误）。

函数指针写法还允许参数做隐式类型转换（`int` 信号连接 `double` 槽），可以连接 lambda 和任何成员函数。**新代码都应该用函数指针写法**，字符串写法只在读老代码时需要认识。

> 少数信号有重载，例如一个类同时有 `valueChanged(int)` 和 `valueChanged(QString)`，`&Class::valueChanged` 就有歧义，要用 `qOverload<int>(&Class::valueChanged)` 选出其中一个。

## 6. moc 生成了什么

### 6.1 忘了写 Q_OBJECT

把 `counter.h` 中的 `Q_OBJECT` 删掉，重新编译：

```
.../qobject.h:253:23: error: static assertion failed due to requirement 'QtPrivate::HasQ_OBJECT_Macro<Counter>::Value': No Q_OBJECT in the class with the signal
main.cpp:11:14: note: in instantiation of function template specialization 'QObject::connect<void (Counter::*)(int), void (Counter::*)(int)>' requested here
```

Qt 用 `static_assert`（C++11 专栏《enum class 与 static_assert》）在编译时检查出了这个错误。那 `Q_OBJECT` 到底提供了什么？

### 6.2 打开 moc 的输出

构建目录中，moc 为 `counter.h` 生成了 `counter_autogen/.../moc_counter.cpp`，一共 128 行。挑出关键的部分看。

**信号的函数体**，在文件的最后：

```cpp
// SIGNAL 0
void Counter::valueChanged(int _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}
```

第 2 节说信号「只有声明，没有函数体」，函数体就在这里。信号就是一个普通的成员函数，`emit valueChanged(v)` 就是调用它，它把「我是谁（`this`）、我是第几个信号（`0`）、参数是什么」交给 `QMetaObject::activate`。`activate` 找到这个信号的连接列表，依次调用每一个槽。

**类的元信息**，以字符串的形式保存了信号和槽的名字、参数类型：

```cpp
    QtMocHelpers::StringRefStorage qt_stringData {
        "Counter",
        "valueChanged",
        "",
        "newValue",
        "setValue",
        "value"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'valueChanged'
        QtMocHelpers::SignalData<void(int)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 3 },
        }}),
        // Slot 'setValue'
        QtMocHelpers::SlotData<void(int)>(4, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 5 },
        }}),
    };
```

这些信息保存在一个静态对象 `Counter::staticMetaObject` 中，叫作**元对象**（meta-object）。第 5 节的字符串写法就是在这里按名字查找的；上一篇用 `metaObject()->className()` 打印出类名，也是从这里读的。

**按编号调用函数**：

```cpp
void Counter::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<Counter *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->valueChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 1: _t->setValue((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        default: ;
        }
    }
    // ...
}
```

参数被打包成一个 `void *` 数组 `_a`，按编号 `_id` 分派到具体的函数。字符串写法的连接、跨线程的连接（参数要先拷贝下来，稍后在另一个线程中调用），都通过这个函数调用槽。

`Q_OBJECT` 宏的作用就是在类中**声明**这些成员：`staticMetaObject`、`metaObject()`、`qt_metacall()` 等，moc 负责**定义**它们。没有 `Q_OBJECT`，就没有元对象，这个类就不能有自己的信号。

C++ 本身没有「运行时按名字查找成员函数」的能力（反射），Qt 用一个代码生成器 moc 补上了它。这也是 Qt 程序必须经过 moc 处理的原因。除了信号与槽，元对象系统还支持属性（`Q_PROPERTY`）、运行时类型信息（`qobject_cast`）、枚举值和名字的转换（上一篇打印事件类型时用的 `QMetaEnum`）。

## 7. 开销有多大

信号要经过 `activate`、查找连接列表、通过函数指针调用，肯定比直接调用慢。慢多少？各调用 1000 万次，算出每次的平均时间：

```cpp
struct Sink : QObject {
    long sum = 0;
    __attribute__((noinline)) void add(int v) { sum += v; }   // 不让编译器内联，比较才公平
};

// 直接调用
for (int i = 0; i < N; ++i) sink.add(i);
// std::function
std::function<void(int)> f = [&](int v) { sink.add(v); };
for (int i = 0; i < N; ++i) f(i);
// 信号：没有连接、1 个槽、10 个槽
for (int i = 0; i < N; ++i) emit c.valueChanged(i);
```

`add` 不加 `noinline` 时，编译器会把整个循环优化成一个公式，「直接调用」测出来是 0.0 纳秒，没有意义。运行三次，取其中一次：

```
直接调用                0.6 ns/次
std::function           0.7 ns/次
信号，没有连接          2.2 ns/次
信号 → 1 个槽           7.8 ns/次
信号 → 10 个槽         33.9 ns/次
```

三次的结果很稳定：直接调用 0.5～1.0 纳秒，`std::function` 0.6～1.2 纳秒，没有连接的信号 2.2～2.8 纳秒，一个槽 7.8～8.0 纳秒，十个槽 32.8～33.9 纳秒。

- 发出一个信号、调用一个槽，大约是直接调用的 10 倍。
- 即使没有任何连接，`emit` 也要 2 纳秒左右：要进入 `activate` 检查有没有连接。
- 槽越多越慢，每多一个槽大约多 3 纳秒。

8 纳秒是什么概念？一个界面程序每秒处理的事件和信号通常是几百到几千个，信号本身的开销加起来不到 0.01 毫秒，和绘制一帧画面（几毫秒）相比完全可以忽略。所以在界面代码中，可以放心地用信号与槽来解耦。只有在每秒执行上百万次的紧密循环中（例如逐个像素地处理图片），才需要避免发信号，改用直接调用。

## 8. 小结

- 信号与槽让对象之间可以互相通知而不必互相知道：发送者只管 `emit`，谁接收由 `connect` 决定；对象删除时连接自动断开。
- 自定义信号和槽的类要继承 `QObject`、写 `Q_OBJECT`、放在头文件中交给 moc。信号只声明不实现，槽是普通的成员函数。
- 用函数指针写法 `connect(发送者, &类::信号, 接收者, &类::槽)`，槽也可以是 lambda；连接 lambda 时传入上下文对象。拼写和类型错误会在编译时被发现。
- 同一个线程中，`emit` 就是函数调用：槽按连接顺序立即同步执行。双向连接时一定要「值没变就不发信号」，否则会无限递归直到栈溢出。
- moc 为每个 `Q_OBJECT` 类生成元对象：信号的函数体（调用 `QMetaObject::activate`）、信号和槽的名字与参数、按编号调用的分派函数。
- 发一个信号调一个槽约 8 纳秒，大约是直接调用的 10 倍，对界面程序来说可以忽略。

前面几篇的代码中，控件都是 `new` 出来的，却从来没有 `delete`。下一篇讲 Qt 的对象树：谁来删除这些对象，什么时候删除，以及在 Qt 中最容易犯的几种内存错误。

## 练习

1. 用一个 `QSlider` 和一个 `QSpinBox` 做一个窗口，范围都是 0～100，拖动滑块时数字跟着变，改数字时滑块跟着动。这里需要自己检查「值没变」吗？为什么？（提示：看第 2 节最后一段。）
2. 给 `Counter` 加一个信号 `reachedLimit()`，数值超过 100 时发出，并把它连接到 `QCoreApplication::quit`。
3. 在第 7 节的实验中，把槽换成一个 lambda（`connect(&c, &Counter::valueChanged, [&](int v) { sink.add(v); })`），开销有变化吗？
4. 在第 6.2 节中，信号 `valueChanged` 的编号是 0，`setValue` 是 1。给 `Counter` 再加一个信号和一个槽，重新构建后看看 moc 的输出中编号是怎样排列的。
