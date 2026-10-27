---
title: 窗口、控件与布局
date: 2026-10-27 10:00:00
updated: 2026-10-27 10:00:00
categories:
  - 客户端开发
  - Qt入门
tags:
  - Qt
  - C++
  - 布局
order: 2
description: 往窗口中放控件有两种办法：用 setGeometry 写死坐标，或者交给布局。本文把同一个登录框按两种办法各写一遍，把窗口从 260 宽拉到 460 宽：写死坐标的控件缩在左上角，用布局的输入框从 212 变成 372 像素宽，按钮始终贴着右下角。macOS 上 QFormLayout 默认不拉伸输入框，宽度停在 125 不动，要改一个策略才行。还会实测控件的建议大小、伸缩系数和大小策略：三个按钮按 1:2:1 分配宽度，固定策略的按钮在 300 和 600 宽的窗口中都是 39 像素。最后看 QMainWindow 的菜单栏、工具栏和状态栏，以及控件被放进布局时父对象是怎样变化的。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：认识 `QWidget`、`QMainWindow`、`QDialog` 三种窗口，以及常用的控件；理解为什么要用布局，会用 `QHBoxLayout`、`QVBoxLayout`、`QGridLayout`、`QFormLayout` 和弹簧排列控件、嵌套布局；理解建议大小 `sizeHint`、大小策略和伸缩系数，能预测布局怎样分配空间；知道控件放进布局后会被自动设置父对象。
>
> **前置知识**：上一篇《环境与第一个 Qt 程序》。
>
> **实验环境**：同上一篇，macOS 27.0，Qt 6.12.0。截图在 Retina 屏上用 `QWidget::grab()` 保存，所以是窗口逻辑尺寸的两倍。

## 1. 控件和窗口

上一篇的窗口只有一个按钮。真实的界面由很多控件组成：标签、输入框、按钮、列表。Qt 中所有能显示在屏幕上的东西都继承自 **`QWidget`**，常用的控件有：

| 类 | 外观 | 常用的函数和信号 |
|---|---|---|
| `QLabel` | 一段文字或一张图片 | `setText`、`setPixmap` |
| `QPushButton` | 按钮 | `clicked` |
| `QLineEdit` | 单行输入框 | `text`、`setEchoMode`（密码）、`textChanged`、`returnPressed` |
| `QTextEdit` | 多行文本编辑 | `toPlainText`、`append` |
| `QCheckBox` | 复选框 | `isChecked`、`toggled` |
| `QRadioButton` | 单选按钮 | 同一个父控件中的单选按钮互斥 |
| `QComboBox` | 下拉框 | `addItem`、`currentIndex`、`currentIndexChanged` |
| `QSpinBox` | 数字输入框 | `setRange`、`value`、`valueChanged` |
| `QSlider` | 滑块 | `setRange`、`valueChanged` |
| `QListWidget`、`QTableWidget` | 列表、表格 | 第 2 章会换成更高效的 Model/View |

控件之间有**父子关系**：创建控件时把另一个控件作为父对象传给它，它就显示在父控件的内部；没有父对象的控件显示为一个独立的窗口。按照用途，Qt 提供了三种常用的窗口：

- **`QWidget`**：最普通的窗口，里面放什么完全由自己决定。
- **`QMainWindow`**：应用程序的主窗口，预留了菜单栏、工具栏、状态栏和中间的主区域（第 5 节）。
- **`QDialog`**：对话框，例如设置、登录、确认。它可以「模态」地显示：`exec()` 打开对话框后，直到用户关闭它才返回，期间不能操作其他窗口。

## 2. 写死坐标的问题

最直接的做法是给每个控件指定位置和大小。`setGeometry(x, y, 宽, 高)` 的坐标相对于父控件的左上角：

```cpp
QWidget window;
window.setWindowTitle("绝对定位");

QLabel *label = new QLabel("用户名", &window);
QLineEdit *edit = new QLineEdit(&window);
QPushButton *button = new QPushButton("登录", &window);
label->setGeometry(20, 20, 60, 28);     // x, y, 宽, 高
edit->setGeometry(80, 20, 160, 28);
button->setGeometry(160, 60, 80, 32);

window.resize(260, 110);
window.show();
```

控件用 `new` 创建，构造时把 `&window` 传入作为父对象。不用自己 `delete`：父对象析构时会把子对象一起删除，第 1.4 篇专门讲这个机制。

窗口大小正好时，一切正常：

<img src="/img/client/absolute-small.png" alt="绝对定位，260×110 的窗口" width="260">

用户把窗口拉大到 460×200：

<img src="/img/client/absolute-large.png" alt="绝对定位，窗口拉大到 460×200" width="460">

控件全都缩在左上角，右边和下面空出一大片。写死坐标还有更多的问题：

- 换一个系统或字体，文字的宽度就不一样了：在 macOS 上刚好放得下的「用户名」，在 Windows 的另一种字体下可能被截断；
- 界面翻译成英文，「登录」变成「Sign in」，按钮放不下；
- 用户在系统中调大了字号，所有控件都要重新计算。

窗口能缩放的界面，每个控件的位置都要根据窗口大小重新算。这件事 Qt 的**布局**（layout）会替我们做。

## 3. 布局

### 3.1 四种布局

| 布局 | 排列方式 |
|---|---|
| `QHBoxLayout` | 从左到右排成一行 |
| `QVBoxLayout` | 从上到下排成一列 |
| `QGridLayout` | 网格，`addWidget(控件, 行, 列, 占几行, 占几列)` |
| `QFormLayout` | 两列的表单：左边标签，右边输入框，`addRow("标签", 控件)` |

布局本身不显示，它只负责计算其中每个控件的位置和大小。布局中可以放控件，也可以放另一个布局（嵌套），还可以放**弹簧**（`addStretch()`），弹簧会占掉多余的空间，把两边的控件推开。

### 3.2 用布局重写登录框

一个登录框可以拆成两部分：上面是表单（用户名、密码、记住我），下面是一行按钮，靠右对齐。用一个纵向布局把两部分叠起来：

```
QVBoxLayout（root）
├── QFormLayout（form）
│   ├── 用户名  [QLineEdit]
│   ├── 密码    [QLineEdit]
│   └──         [QCheckBox]
└── QHBoxLayout（buttons）
    ├── 弹簧
    ├── [取消]
    └── [登录]
```

```cpp
#include <QApplication>
#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QWidget window;
    window.setWindowTitle("登录");

    auto *user = new QLineEdit;
    auto *password = new QLineEdit;
    password->setEchoMode(QLineEdit::Password);
    password->setPlaceholderText("至少 8 位");
    auto *remember = new QCheckBox("记住我");

    auto *form = new QFormLayout;           // 左边标签、右边输入框
    form->addRow("用户名", user);
    form->addRow("密码", password);
    form->addRow("", remember);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);   // 见 3.3

    auto *ok = new QPushButton("登录");
    auto *cancel = new QPushButton("取消");
    ok->setDefault(true);                   // 按回车相当于点「登录」
    auto *buttons = new QHBoxLayout;        // 横向排列两个按钮
    buttons->addStretch();                  // 弹簧：把按钮推到右边
    buttons->addWidget(cancel);
    buttons->addWidget(ok);

    auto *root = new QVBoxLayout(&window);  // 纵向：表单在上，按钮在下
    root->addLayout(form);
    root->addLayout(buttons);

    window.resize(300, 140);
    window.show();
    return app.exec();
}
```

注意这里的控件创建时都**没有**传父对象，只有最外层的 `root` 在构造时传入了 `&window`，意思是「这个布局负责管理 `window` 的内部」。

在 300 宽和 460 宽的窗口中各运行一次：

<img src="/img/client/login-small.png" alt="用布局的登录框，300 宽" width="300">

<img src="/img/client/login-large.png" alt="用布局的登录框，拉大到 460×200" width="460">

在程序中打印输入框的宽度和「登录」按钮的位置：

```
窗口 300x161: user 宽 212，ok 位置 (223, 109)
窗口 460x200: user 宽 372，ok 位置 (383, 148)
```

窗口变宽了 160 像素，输入框正好也变宽了 160 像素（212 → 372），按钮的横坐标也右移了 160，始终贴着右边。多出来的高度被表单和按钮之间的空白吸收了，按钮始终贴着底部。

还有一个细节：代码中要求的大小是 300×140，实际的窗口却是 300×161。布局会根据其中的控件算出窗口的**最小大小**，这几个控件竖着排列至少要 161 像素高，窗口不能比它小，用户拖动窗口边缘时也缩不到更小。写死坐标时就没有这层保护，窗口缩小后控件会被直接裁掉。

### 3.3 不同平台的默认行为不一样

上面的代码中有一行 `setFieldGrowthPolicy`，去掉它，在 macOS 上运行：

<img src="/img/client/login-default.png" alt="macOS 上 QFormLayout 的默认样子" width="460">

```
窗口 300x161: user 宽 125，ok 位置 (223, 109)
窗口 460x200: user 宽 125，ok 位置 (383, 148)
```

输入框停在 125 像素不动，整个表单在窗口中水平居中。这不是错误，而是 Qt 在模仿各个平台的习惯：macOS 的设计规范是表单居中、输入框保持自然宽度，所以 Qt 的 macOS 样式把 `QFormLayout` 的默认值设成了「输入框不拉伸」；Windows 和 Linux 上默认拉伸。这类差异是跨平台开发中常见的问题：同一份代码，在自己的电脑上看着没问题，换一个平台就变了样。需要统一的行为时，就像这里一样显式设置。

### 3.4 加入布局后，控件的父对象是谁

创建 `user` 时没有给它父对象。打印一下它的父对象在加入布局前后的变化：

```cpp
std::printf("加入布局前 user 的父对象: %s\n", user->parent() ? "有" : "无");
// ... root->addLayout(form) 之后 ...
std::printf("加入布局后 user 的父对象: %s，是 window: %d\n",
            user->parent()->metaObject()->className(), user->parent() == &window);
```

```
加入布局前 user 的父对象: 无
加入布局后 user 的父对象: QWidget，是 window: 1
```

布局把它管理的控件的父对象自动设置成了布局所在的窗口 `window`。所以控件既不会显示成独立的窗口，也会在 `window` 析构时被一起删除。布局只负责排列，不拥有控件，控件的父对象永远是一个 `QWidget`。

## 4. 布局怎样分配空间

布局怎么决定每个控件有多宽？它依据三样东西：控件的建议大小、大小策略和伸缩系数。

### 4.1 建议大小

每个控件都会报告自己的**建议大小** `sizeHint()`，就是「显示我的内容最合适的大小」，和最小建议大小 `minimumSizeHint()`：

```cpp
void print(const char *name, QWidget *w)
{
    QSize h = w->sizeHint(), m = w->minimumSizeHint();
    std::printf("%-22s sizeHint %3dx%-3d minimumSizeHint %3dx%-3d\n", name,
                h.width(), h.height(), m.width(), m.height());
}
```

```
QPushButton(确定)    sizeHint  57x32  minimumSizeHint  57x32
QPushButton(很长)    sizeHint 201x32  minimumSizeHint 201x32
QLabel(标签)         sizeHint  27x16  minimumSizeHint  27x16
QLineEdit              sizeHint 125x21  minimumSizeHint  19x21
窗口（按钮 + 输入框） sizeHint 242x61  minimumSizeHint 136x61
没调用 resize，窗口大小 242x61
```

（中文字符在等宽输出中占两格，所以前几行的列没有完全对齐。）

- 按钮和标签的建议大小由文字决定：文字越长越宽。这就是布局能适应翻译和字体变化的原因。
- 输入框的建议宽度是一个固定值 125，3.3 中 macOS 上不拉伸的输入框就停在这个宽度；它的最小建议宽度只有 19，说明它可以被压得很窄。
- 有布局的窗口，建议大小由布局根据其中的控件算出来。没有调用 `resize()` 的窗口，`show()` 时就用这个大小。

### 4.2 大小策略和伸缩系数

建议大小只是「最合适」的大小。窗口比建议大小大时，多出来的空间分给谁，由**大小策略**（`QSizePolicy`）决定。常用的几种：

| 策略 | 含义 | 默认使用的控件 |
|---|---|---|
| `Fixed` | 只用建议大小，不放大也不缩小 | 按钮和输入框的高度 |
| `Preferred` | 建议大小最好，也可以放大或缩小 | 标签 |
| `Expanding` | 尽量占满可用空间 | 输入框的宽度 |
| `Minimum` | 至少是建议大小，可以更大 | 按钮的宽度 |

几个控件都想要多余的空间时，按**伸缩系数**（stretch）分配。

用三个按钮做实验：横向布局中放 A、B、C，都设成 `Expanding`，再分别试三种配置。窗口宽 300 和 600 时，三个按钮的宽度：

```cpp
QWidget window;
auto *row = new QHBoxLayout(&window);
QPushButton *a = new QPushButton("A"), *b = new QPushButton("B"), *c = new QPushButton("C");
for (auto *p : {a, b, c}) {
    row->addWidget(p);
    p->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);   // 横向尽量占满
}
// 配置 stretch：
row->setStretch(0, 1);
row->setStretch(1, 2);
row->setStretch(2, 1);
// 配置 fixed：
b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);       // B 保持建议大小
```

```
expanding 窗口宽 300: A  80  B  80  C  80
expanding 窗口宽 600: A 180  B 180  C 180
stretch   窗口宽 300: A  60  B 120  C  60
stretch   窗口宽 600: A 135  B 270  C 135
fixed     窗口宽 300: A 101  B  39  C 100
fixed     窗口宽 600: A 251  B  39  C 250
```

- **都是 `Expanding`**：平分。窗口宽 300 时，减去左右各 20 的边距和按钮之间的间距，剩下 240，每个 80。
- **伸缩系数 1:2:1**：B 的宽度始终是 A、C 的两倍：60/120/60，135/270/135。
- **B 是 `Fixed`**：B 始终是建议宽度 39，不管窗口多宽；A 和 C 分掉其余的空间（总宽是奇数时，两边差 1 像素）。

弹簧 `addStretch()` 也是同样的道理：它是一个自己什么都不显示、大小策略为 `Expanding` 的空白。3.2 中的「取消」「登录」两个按钮的宽度是 `Minimum` 策略，不主动要多余的空间，多出来的宽度就全给了弹簧，按钮被推到了右边。

调界面时最常见的问题是「某个控件不肯变大」或者「某个控件占了太多地方」，先检查它的大小策略和所在布局的伸缩系数，一般就能找到原因。

## 5. 主窗口 QMainWindow

一个完整的应用程序通常有菜单、工具栏、状态栏。`QMainWindow` 已经把这些位置留好了：

```
┌──────────────────────────────┐
│ 菜单栏 menuBar()              │
├──────────────────────────────┤
│ 工具栏 addToolBar()           │
├──────────────────────────────┤
│                              │
│ 中心控件 setCentralWidget()    │
│                              │
├──────────────────────────────┤
│ 状态栏 statusBar()            │
└──────────────────────────────┘
```

写一个记事本的外壳：

```cpp
#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QStatusBar>
#include <QTextEdit>
#include <QToolBar>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QMainWindow window;
    window.setWindowTitle("记事本");

    QMenu *file = window.menuBar()->addMenu("文件");
    QAction *open = file->addAction("打开…");
    QAction *quit = file->addAction("退出");
    QObject::connect(quit, &QAction::triggered, &app, &QApplication::quit);

    QToolBar *tools = window.addToolBar("工具");
    tools->addAction(open);              // 同一个动作，既在菜单中，也在工具栏上
    tools->addAction("保存");

    window.setCentralWidget(new QTextEdit);
    window.statusBar()->showMessage("就绪");

    window.resize(420, 260);
    window.show();
    return app.exec();
}
```

<img src="/img/client/mainwin.png" alt="QMainWindow：工具栏、中心的文本框和状态栏" width="420">

**动作**（`QAction`）表示一个用户可以执行的命令，例如「打开」。同一个动作可以同时放在菜单、工具栏上，还可以设置快捷键；不管从哪里触发，都发出同一个 `triggered` 信号，只要连接一次。

截图中没有菜单栏。打印一下：

```cpp
std::printf("菜单栏是否原生: %d，可见: %d\n", window.menuBar()->isNativeMenuBar(),
            window.menuBar()->isVisible());
```

```
菜单栏是否原生: 1，可见: 0
```

macOS 的菜单不在窗口中，而在屏幕顶部的系统菜单栏上。Qt 在 macOS 上自动把菜单栏换成了系统原生的菜单，窗口里的 `QMenuBar` 控件是隐藏的。在 Windows 和 Linux 上运行，菜单栏会出现在窗口顶部。工具栏上的「打开…」显示成了「打开」：省略号的意思是「点了之后还会弹出对话框」，只在菜单中有意义，Qt 在工具按钮上会自动去掉它（各个平台都是这样）。

## 6. 小结

- 所有控件都继承自 `QWidget`；有父对象的控件显示在父控件内部，没有父对象的是独立的窗口。常用的窗口有 `QWidget`、`QMainWindow`、`QDialog`。
- 用 `setGeometry` 写死坐标，窗口缩放、换字体、换语言时都会出问题。用布局，控件的位置和大小随窗口自动调整，窗口还会有合理的最小大小。
- 四种布局：横向、纵向、网格、表单；布局可以嵌套，弹簧用来推开控件。
- 控件放进布局后，父对象会被自动设为布局所在的窗口。
- 布局按建议大小、大小策略和伸缩系数分配空间。
- 同一份代码在不同平台上的默认行为可能不同（表单是否拉伸、菜单栏的位置），需要统一时要显式设置。

这两篇中已经多次用到 `connect`：点击按钮退出、点击菜单退出。下一篇专门讲信号与槽：怎样定义自己的信号，`connect` 背后发生了什么，以及 moc 到底生成了什么代码。

## 练习

1. 用 `QGridLayout` 做一个计算器的按键区：4 列 5 行，数字 0 占两列。窗口拉大时，所有按键一起变大。
2. 把 3.2 的登录框改成继承 `QDialog` 的类 `LoginDialog`，点「登录」时调用 `accept()`，点「取消」时调用 `reject()`。在 `main` 中用 `if (dialog.exec() == QDialog::Accepted)` 判断用户点了哪个。
3. 在 4.2 的实验中，把窗口宽度改成 100，比三个按钮的建议宽度之和还小，会发生什么？窗口能缩到 100 吗？
4. 给第 5 节的「打开…」动作设置快捷键 `QKeySequence::Open`，在 macOS 上它是什么键？在 Windows 上呢？
