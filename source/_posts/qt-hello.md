---
title: 环境与第一个 Qt 程序
date: 2026-10-27 09:00:00
updated: 2026-10-27 09:00:00
categories:
  - 客户端开发
  - Qt入门
tags:
  - Qt
  - C++
  - CMake
order: 1
description: 装好 Qt 6，用 CMake 写一个只有一个按钮的窗口，点一下就退出。程序只有 74 KB，却依赖 6.5 MB 的 QtCore、7.4 MB 的 QtGui 和 6.6 MB 的 QtWidgets，还要在运行时加载平台插件 libqcocoa。去掉最后一行的 app.exec()，窗口一闪就没了；在控件上装一个事件过滤器，可以看到 show() 返回时窗口一次都还没画过，第一次 Paint 发生在进入 exec() 之后的第 70 毫秒。先创建按钮、后创建 QApplication，程序直接崩溃，退出码 134。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：知道 Qt 是什么、由哪些模块组成；会安装 Qt 6，用 CMake 构建一个 Qt 程序；读懂最小的 Qt 程序中每一行的作用；理解 `QApplication` 和事件循环 `exec()`：窗口是在事件循环中画出来的；知道 Qt 程序运行时依赖哪些库和插件。
>
> **前置知识**：C++ 的类、继承和多态（C/C++ 专栏《C++基础》）；会用 CMake 构建项目（《开发环境与编程工具》第 6 节）。
>
> **实验环境**：macOS 27.0（Apple M6），Homebrew 安装的 Qt 6.12.0，Apple Clang 21，CMake 4.4。本专栏的代码在 Linux 和 Windows 上同样可以编译，平台不同的地方会单独说明。

## 1. Qt 是什么

前面几个专栏写的程序都运行在终端里：读标准输入，写标准输出。日常用的软件大多不是这样，它们有窗口、按钮、菜单，用鼠标和键盘操作。这样的程序叫作**图形界面程序**（GUI），运行在用户电脑上、和服务器通信的这一端叫作**客户端**。

写图形界面要和操作系统的窗口系统打交道：macOS 是 AppKit，Windows 是 Win32，Linux 是 X11 或 Wayland。它们的接口完全不同，直接用的话，同一个程序要写三遍。

**Qt** 是一个跨平台的 C++ 应用框架。它把各个平台的窗口系统包装成同一套接口：同一份代码，在 macOS、Windows、Linux 上编译，就能得到各自平台的原生程序。很多常见的软件是用 Qt 写的，例如 VirtualBox、OBS Studio、WPS Office 的 Linux 版、Telegram 桌面版，还有 KDE 桌面环境。

Qt 不只是界面库，它由许多**模块**组成，常用的有：

| 模块 | 作用 | 本专栏 |
|---|---|---|
| Qt Core | 对象模型、信号与槽、事件循环、线程、容器、文件、JSON | 第 1、3 章 |
| Qt Gui | 窗口、绘图、字体、图片、键盘和鼠标事件 | 第 2 章 |
| Qt Widgets | 按钮、输入框、表格等传统桌面控件 | 第 1、2 章 |
| Qt Network | TCP、UDP、HTTP | 第 4 章 |
| Qt Sql | 访问 SQLite、MySQL 等数据库 | 第 4 章 |
| Qt Quick / QML | 用声明式语言写动画丰富的界面，常用于移动端和嵌入式 | 不涉及 |

写界面有两条路：**Qt Widgets** 用 C++ 代码创建控件，是传统的桌面程序写法；**Qt Quick** 用一种类似 JavaScript 的语言 QML 描述界面。本专栏用 Qt Widgets，因为它就是普通的 C++，前面专栏学过的类、继承、虚函数、lambda 全都用得上，也更容易看清背后的机制。

> **许可证**：Qt 有开源版（LGPL v3 / GPL v3）和商业版。开源版可以免费用来学习和开发；用 LGPL 发布闭源软件时，需要动态链接 Qt 并允许用户替换 Qt 库。本专栏只涉及开源版。

## 2. 安装 Qt 6

安装方式按平台选一种：

| 平台 | 安装方式 |
|---|---|
| macOS | `brew install qt` |
| Ubuntu / Debian | `sudo apt install qt6-base-dev qt6-tools-dev cmake g++` |
| Windows | 从 qt.io 下载在线安装器 Qt Online Installer，选择 Qt 6 的 MinGW 或 MSVC 版本 |

macOS 上的 `brew install qt` 会把所有模块都装上，共 38 个依赖包，在实验的机器上占了 843 MB。只想要最基础的部分，可以只装 `qtbase`（Core、Gui、Widgets、Network、Sql 都在里面）。

装好后确认版本：

```bash
qtpaths --qt-version
```

```
6.12.0
```

> Qt 官方还提供了一个 IDE：**Qt Creator**，集成了代码编辑、界面设计器、调试器，打开 `CMakeLists.txt` 就能直接构建运行，是写 Qt 程序最方便的工具（Windows 的在线安装器会一起装上，macOS 用 `brew install --cask qt-creator`）。本专栏为了看清每一步做了什么，用命令行加 CMake，任何编辑器都可以。

## 3. 第一个程序

新建目录 `hello`，在其中写两个文件。`main.cpp`：

```cpp
#include <QApplication>
#include <QPushButton>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QPushButton button("你好，Qt！");
    button.resize(240, 80);
    QObject::connect(&button, &QPushButton::clicked, &app, &QApplication::quit);
    button.show();

    return app.exec();
}
```

`CMakeLists.txt`：

```cmake
cmake_minimum_required(VERSION 3.16)
project(hello LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

qt_add_executable(hello main.cpp)
target_link_libraries(hello PRIVATE Qt6::Widgets)
```

构建并运行：

```bash
cmake -B build -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build build
./build/hello
```

```
[ 20%] Built target hello_autogen
[ 40%] Building CXX object CMakeFiles/hello.dir/hello_autogen/mocs_compilation.cpp.o
[ 60%] Building CXX object CMakeFiles/hello.dir/main.cpp.o
[ 80%] Linking CXX executable hello
[100%] Built target hello
```

屏幕上出现一个窗口，整个窗口就是一个按钮，点一下程序就退出了：

<img src="/img/client/hello.png" alt="第一个 Qt 程序：一个按钮" width="240">

`CMAKE_PREFIX_PATH` 告诉 CMake 到哪里找 Qt。Linux 上用 apt 装的 Qt 在系统默认路径中，可以省略；Windows 上要写成 Qt 的安装目录，例如 `C:/Qt/6.12.0/mingw_64`。

### 3.1 CMakeLists.txt 中的 Qt 部分

前三行和普通的 C++ 项目一样。和 Qt 有关的是后面四行：

- `find_package(Qt6 REQUIRED COMPONENTS Widgets)`：找到 Qt 6 的 Widgets 模块。它依赖的 Core 和 Gui 会自动带上。以后用到网络，就在后面加上 `Network`。
- `qt_standard_project_setup()`：打开 Qt 项目的常用设置，最重要的是 **AUTOMOC**：构建时自动对源文件运行 Qt 的代码生成器 **moc**。构建输出中的 `hello_autogen` 和 `mocs_compilation.cpp` 就是它。这个程序没有自定义的类，所以 moc 什么也没生成；第 1.3 篇写自己的信号时，会打开 moc 生成的代码看一看。
- `qt_add_executable`：相当于 `add_executable`，另外做一些 Qt 需要的处理。
- `target_link_libraries(... Qt6::Widgets)`：链接 Widgets 模块，同时得到它的头文件路径。

> 老的 Qt 项目用 Qt 自己的构建工具 **qmake**，项目文件的扩展名是 `.pro`。Qt 6 起官方推荐 CMake，新项目用 CMake 即可，读到 `.pro` 文件时知道它是什么就行。

### 3.2 main.cpp 逐行看

**`QApplication app(argc, argv);`**

每个 Qt Widgets 程序都要有且只有一个 `QApplication` 对象，而且要在创建任何控件之前创建。它负责初始化：连接窗口系统、加载平台插件、读取系统的字体和颜色设置、处理命令行参数（所以要把 `argc`、`argv` 传给它）。

**`QPushButton button("你好，Qt！");`**

创建一个按钮。Qt 中所有的控件都继承自 `QWidget`，一个没有父控件的 `QWidget` 显示出来就是一个独立的窗口，所以这里的按钮自己就是一个窗口。下一篇会把多个控件放进一个窗口中。

字符串字面量直接写中文就可以：Qt 6 默认把源文件中的字符串当作 UTF-8，转换成 Qt 的字符串类 `QString`。`QString` 内部用 UTF-16 保存，`size()` 是 UTF-16 码元的个数，不是字节数。

**`button.resize(240, 80);`**

设置窗口大小，单位是**逻辑像素**。实验用的 Mac 是 Retina 屏，一个逻辑像素对应 2×2 个物理像素，所以上面的截图是 480×160。Qt 会自动处理高分辨率屏，代码中都按逻辑像素写。

**`QObject::connect(&button, &QPushButton::clicked, &app, &QApplication::quit);`**

**信号与槽**：当 `button` 发出 `clicked` 信号（被点击）时，调用 `app` 的 `quit` 函数。这是 Qt 中对象之间通信的方式，第 1.3 篇专门讲。

**`button.show();`**

让窗口显示出来。默认创建的窗口是隐藏的。

**`return app.exec();`**

进入**事件循环**，直到 `quit()` 被调用才返回。接下来的两个实验专门看这一行。

## 4. 事件循环：窗口是怎样画出来的

### 4.1 去掉 exec() 会怎样

把最后一行改成 `return 0;`，并在 `show()` 后面打印一下窗口的状态：

```cpp
    button.show();
    std::printf("show() 返回了，isVisible = %d\n", button.isVisible());
    return 0;      // 没有调用 app.exec()
```

```bash
/usr/bin/time -p ./noexec
```

```
show() 返回了，isVisible = 1
real 0.10
user 0.04
sys 0.01
```

`isVisible()` 是 1，Qt 认为窗口已经显示了，但屏幕上只闪了一下（大多数时候什么也看不到），0.1 秒后程序就结束了。

原因是：图形界面程序是**事件驱动**的。用户点击、按键、窗口需要重画、定时器到时间，都会变成一个个**事件**（event），放进事件队列。`exec()` 中是一个循环，不断地从队列中取出事件，交给对应的对象处理：

```
while (还没有调用 quit()) {
    等待，直到有事件到来;       // 没有事件时线程睡眠，不占 CPU
    取出一个事件;
    把它发给目标对象处理;        // 例如：鼠标点击 → 按钮 → 发出 clicked 信号
}
```

这和网络专栏《IO 多路复用》中 epoll 服务器的主循环是同一个结构：程序的大部分时间都在等待，有事发生才醒来处理。事实上 Qt 在 Linux 上的事件循环底层用的就是 `poll`，在 macOS 上用的是系统的 `CFRunLoop`。

没有事件循环，窗口就收不到「需要绘制」的事件，`main` 一返回，程序就结束了。

### 4.2 看看按钮收到了哪些事件

Qt 允许在一个对象上安装**事件过滤器**（event filter），发给它的每个事件都会先经过过滤器。用它把按钮收到的事件全部打印出来，同时记下时间：

```cpp
#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QMetaEnum>
#include <QPushButton>
#include <QTimer>
#include <cstdio>

QElapsedTimer clock_;

// 把发给 button 的每个事件打印出来
class Spy : public QObject {
public:
    bool eventFilter(QObject *, QEvent *e) override
    {
        std::printf("%7.1f ms  %s\n", clock_.nsecsElapsed() / 1e6,
                    QMetaEnum::fromType<QEvent::Type>().valueToKey(e->type()));
        return false;   // 不拦截，照常交给 button
    }
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QPushButton button("你好，Qt！");
    Spy spy;
    button.installEventFilter(&spy);
    button.resize(240, 80);

    clock_.start();
    std::printf("---- 调用 show()\n");
    button.show();
    std::printf("---- show() 返回，进入 exec()\n");
    QTimer::singleShot(500, &app, &QApplication::quit);
    return app.exec();
}
```

`QTimer::singleShot(500, ...)` 让程序在 500 毫秒后自动调用 `quit()`，不用手动关窗口。运行结果：

```
---- 调用 show()
   18.0 ms  PlatformSurface
   18.1 ms  WinIdChange
   18.1 ms  WindowIconChange
   18.1 ms  Polish
   18.1 ms  PaletteChange
   18.1 ms  Move
   18.1 ms  Resize
   18.1 ms  Show
   18.1 ms  CursorChange
   18.2 ms  Move
   34.4 ms  ShowToParent
---- show() 返回，进入 exec()
   52.8 ms  PolishRequest
   52.8 ms  UpdateLater
   52.8 ms  UpdateRequest
   70.4 ms  Paint
  167.5 ms  WindowActivate
  167.6 ms  ActivationChange
  167.6 ms  InputMethodQuery
  167.6 ms  FocusIn
  167.6 ms  InputMethodQuery
  214.5 ms  LayoutRequest
  214.5 ms  UpdateRequest
  214.6 ms  Paint
  216.1 ms  InputMethodQuery
  535.7 ms  Close
  535.7 ms  FocusAboutToChange
  535.7 ms  WindowDeactivate
  535.7 ms  ActivationChange
  535.7 ms  FocusOut
  548.5 ms  Hide
  548.5 ms  HideToParent
  548.5 ms  PlatformSurface
  548.5 ms  WinIdChange
```

这份输出可以分成几段来读：

1. **`show()` 之内**（18～34 ms）：Qt 向系统申请了真正的窗口（`PlatformSurface`、`WinIdChange`），确定了样式（`Polish`）、位置和大小（`Move`、`Resize`），把状态标记为显示（`Show`）。这些事件是 `show()` 内部直接发给按钮的。
2. **`show()` 返回时，窗口一次都还没有画过**：整段中没有 `Paint`。`show()` 只是「请求」显示，把一个「需要重画」的请求（`UpdateRequest`）放进了队列。
3. **进入 `exec()` 后**：事件循环取出 `UpdateRequest`，第一次 `Paint` 在 70 ms 时发生，按钮把自己画了出来。
4. 约 100 ms 后窗口成为活动窗口（`WindowActivate`），按钮获得键盘焦点（`FocusIn`），焦点框的变化又触发了一次重画。
5. 500 ms 时 `quit()` 关闭窗口（`Close`、`Hide`），事件循环结束，`exec()` 返回。

这就解释了 4.1 的现象：没有 `exec()`，第 3 步永远不会发生。

由此还能得出一条贯穿整个专栏的规则：**界面上的一切都要靠事件循环推动**。如果在某个按钮的处理函数里做了一件耗时 5 秒的事，这 5 秒内事件循环没有机会取下一个事件，窗口不会重画，也不响应点击，系统会显示「未响应」。第 3 章会专门重现这个问题，并用多线程解决它。

### 4.3 先创建控件会怎样

把 `QApplication` 放到按钮之后：

```cpp
    QPushButton button("你好，Qt！");   // 先创建控件
    QApplication app(argc, argv);      // 后创建 QApplication
```

编译没有问题，运行时：

```
QWidget: Must construct a QApplication before a QWidget
```

```bash
echo $?
```

```
134
```

134 = 128 + 6，进程被信号 6（`SIGABRT`）终止（Linux 基础专栏《信号》）。控件在构造时就需要字体、样式、屏幕信息，这些都由 `QApplication` 初始化，所以 Qt 在 `QWidget` 的构造函数中检查到没有 `QApplication`，直接调用 `abort()`。

## 5. 程序依赖了什么

看看生成的可执行文件：

```bash
ls -lh build/hello
otool -L build/hello        # Linux 上用 ldd
```

```
-rwxr-xr-x  1 lucky  wheel    74K Oct 27 20:28 build/hello
build/hello:
	/opt/homebrew/opt/qtbase/lib/QtWidgets.framework/Versions/A/QtWidgets (compatibility version 6.0.0, current version 6.12.0)
	/opt/homebrew/opt/qtbase/lib/QtGui.framework/Versions/A/QtGui (compatibility version 6.0.0, current version 6.12.0)
	/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit (...)
	/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL (...)
	/System/Library/Frameworks/ImageIO.framework/Versions/A/ImageIO (...)
	/System/Library/Frameworks/Metal.framework/Versions/A/Metal (...)
	/opt/homebrew/opt/qtbase/lib/QtCore.framework/Versions/A/QtCore (compatibility version 6.0.0, current version 6.12.0)
	/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit (...)
	...
	/usr/lib/libc++.1.dylib (...)
	/usr/lib/libSystem.B.dylib (...)
```

程序本身只有 74 KB，真正干活的是三个 Qt 的动态库（Linux 基础专栏《静态库与动态库》）：

| 库 | 大小 |
|---|---|
| QtCore | 6.5 MB |
| QtGui | 7.4 MB |
| QtWidgets | 6.6 MB |

列表中虽然有 AppKit，但把 Qt 的窗口、事件翻译成 AppKit 调用的那部分代码并不在这三个库中，而在运行时才加载的**平台插件**里。设置环境变量 `QT_DEBUG_PLUGINS=1` 可以看到加载过程：

```bash
QT_DEBUG_PLUGINS=1 ./build/hello 2>&1 | grep "loaded library"
```

```
qt.core.library: ".../share/qt/plugins/platforms/libqcocoa.dylib" loaded library
qt.core.library: ".../share/qt/plugins/styles/libqmacstyle.dylib" loaded library
```

`libqcocoa` 是 macOS 的平台插件，`libqmacstyle` 让控件画成 macOS 的样子。Windows 上对应的是 `qwindows.dll`，Linux 上是 `libqxcb.so`（X11）或 `libqwayland.so`。这就是 Qt 跨平台的方式：上层代码只调用 Qt 的接口，由插件把它翻译成各个系统的调用。

找不到平台插件，程序就无法启动。用 `-platform` 参数故意指定一个不存在的插件：

```bash
./build/hello -platform nosuch
```

```
qt.qpa.plugin: Could not find the Qt platform plugin "nosuch" in ""
This application failed to start because no Qt platform plugin could be initialized. Reinstalling the application may fix this problem.

Available platform plugins are: offscreen, minimal, cocoa.
```

这条错误值得记住：把 Qt 程序拷到另一台没有装 Qt 的电脑上运行时，最常见的就是它。发布程序时要把 Qt 的动态库**和插件**一起带上，Qt 提供了 `macdeployqt`、`windeployqt` 自动完成这件事，第 5 章打包时会用到。

列表中的 `offscreen` 是一个不显示任何窗口、只在内存中绘制的插件，可以在没有显示器的服务器上运行 Qt 程序，做截图或者自动化测试：

```bash
QT_QPA_PLATFORM=offscreen ./build/hello
```

## 6. 小结

- Qt 是跨平台的 C++ 应用框架，由 Core、Gui、Widgets、Network 等模块组成；本专栏用 Qt Widgets 写桌面界面。
- 用 CMake 构建 Qt 程序：`find_package(Qt6 COMPONENTS ...)`、`qt_standard_project_setup()`（打开 AUTOMOC）、`qt_add_executable`、链接 `Qt6::Widgets`。
- 每个 Widgets 程序有且只有一个 `QApplication`，必须在任何控件之前创建，否则程序直接 abort。
- 没有父控件的控件就是一个窗口；`show()` 只是请求显示，窗口在事件循环中才被画出来。
- `app.exec()` 进入事件循环：等待事件、分发事件，直到 `quit()`。界面上的一切都靠它推动，所以不能在界面线程中做耗时的事。
- Qt 程序依赖 Qt 的动态库和平台插件，发布时两者都要带上。

下一篇往窗口里放更多的控件：输入框、标签、按钮，并用布局让它们自动排列、随窗口缩放。

## 练习

1. 把按钮的文字改成一个很长的句子，不调用 `resize()`，窗口有多大？再改回短文字看看。（提示：控件有一个「建议大小」`sizeHint()`，下一篇会讲。）
2. 在 4.2 的程序中，运行时用鼠标点一下按钮（把 `singleShot` 的时间改长一些），找出点击产生了哪几个事件。
3. 在 4.2 的程序中，让 `eventFilter` 遇到 `QEvent::Paint` 时返回 `true`，会看到什么？为什么？
4. 用 `QT_QPA_PLATFORM=offscreen` 运行 4.2 的程序，事件序列和显示在屏幕上时有什么不同？
