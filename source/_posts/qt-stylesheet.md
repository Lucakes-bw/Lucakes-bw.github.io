---
title: 样式表与深色模式
date: 2026-10-28 11:00:00
updated: 2026-10-28 11:00:00
categories:
  - 客户端开发
  - 界面开发
tags:
  - Qt
  - C++
  - QSS
  - 深色模式
order: 3
description: Qt 样式表（QSS）的语法和 CSS 几乎一样，几十行就能把登录框改成圆角输入框和蓝色主按钮。本文先实测样式表的两个坑：给输入框设置动态属性 error=true 后调用 update()，边框还是灰色 #c8c8cc，要 unpolish/polish 一次才变成红色 #ff3b30；2000 个按钮的窗口，重新设置一次样式表要 40～50 毫秒。接着切换到深色模式：写死了 background: white 的输入框里，白色的文字完全看不见了，改用 palette(base) 才正常。程序运行中切换深色模式时还有两个坑：在 colorSchemeChanged 信号中重设样式表，调色板还是浅色的，没有用；监听调色板变化事件重设样式表，样式表自己又触发了调色板变化，400 毫秒里重设了 15 次。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会写 Qt 样式表：选择器、伪状态、盒模型；会用对象名和动态属性选中特定的控件，知道修改动态属性后要重新应用样式；了解样式表的性能开销；理解调色板 `QPalette` 和样式表的关系；会让程序跟随系统的深色模式，在样式表中用 `palette()` 引用调色板的颜色，并在运行中切换时正确地更新。
>
> **前置知识**：本章前两篇。了解 CSS 的基本语法会更容易理解（不了解也没关系）。
>
> **实验环境**：macOS 27.0，Qt 6.12.0。截图用 `QWidget::grab()` 保存；深色模式的截图用 `QStyleHints::setColorScheme()` 强制切换，系统本身是浅色模式。

## 1. 默认的外观

前面所有的截图都是 Qt 的 macOS 样式画的：Qt 尽量让控件看起来像系统原生的控件。这对工具类软件是好事，用户不需要适应。但很多应用有自己的设计：品牌色、圆角、特别醒目的主按钮。上一章的登录框，用默认样式是这样的：

<img src="/img/client/qss-plain.png" alt="默认的 macOS 样式下的登录框" width="340">

「登录」和「取消」看起来一样，用户分不清哪个是主要操作；错误提示是普通的黑色文字，不醒目。

Qt 改变外观有两种方式：

- **样式表**（Qt Style Sheets，QSS）：语法和网页的 CSS 几乎一样，可以改颜色、边框、圆角、内边距、字体，写起来最方便。
- **调色板**（`QPalette`）和**自定义样式**（继承 `QStyle` / `QProxyStyle`）：更底层，性能更好，但写起来麻烦得多。

本文主要讲样式表，第 4 节讲调色板，因为深色模式离不开它。

## 2. 样式表

### 2.1 一个例子

```cpp
const char *kStyle = R"(
QWidget {
    font-size: 14px;
}
QLineEdit {
    border: 1px solid #c8c8cc;
    border-radius: 6px;
    padding: 5px 8px;
    background: white;
}
QLineEdit:focus {
    border: 2px solid #0a84ff;
    padding: 4px 7px;              /* 边框粗了 1 像素，内边距相应减 1，文字不跳动 */
}
QPushButton {
    border: 1px solid #c8c8cc;
    border-radius: 6px;
    padding: 6px 18px;
    background: #f2f2f7;
}
QPushButton:hover {
    background: #e5e5ea;
}
QPushButton#primary {
    color: white;
    border: none;
    background: #0a84ff;
}
QPushButton#primary:disabled {
    background: #a0c8ff;
}
QLabel#hint {
    color: #ff3b30;
}
)";

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setStyleSheet(kStyle);          // 对整个程序生效
    // ...
    ok->setObjectName("primary");       // 给样式表中的 #primary 选择器用
    hint->setObjectName("hint");
}
```

`R"(...)"` 是 C++11 的原始字符串字面量，里面的换行和引号都不用转义，很适合写样式表。实际的项目中，样式表通常放在一个单独的 `.qss` 文件中，用 Qt 的资源系统编译进程序。

<img src="/img/client/qss-styled.png" alt="应用样式表之后的登录框" width="340">

主按钮变成了蓝色，错误提示变成了红色，获得焦点的输入框有一圈蓝色的边框。

### 2.2 选择器

样式表由一条条**规则**组成：「选择器 { 属性: 值; }」。选择器决定规则作用于哪些控件：

| 选择器 | 匹配 | 例子 |
|---|---|---|
| `QPushButton` | `QPushButton` 及其子类 | 所有按钮 |
| `.QPushButton` | 只匹配 `QPushButton`，不含子类 | |
| `#primary` | `objectName` 为 `primary` 的控件 | 2.1 节的「登录」按钮 |
| `QPushButton#primary` | 两个条件同时满足 | |
| `QLineEdit[error="true"]` | 属性 `error` 的值为 `true` | 第 2.4 节 |
| `QDialog QPushButton` | 在 `QDialog` 里面（任意层级）的按钮 | |
| `QDialog > QPushButton` | `QDialog` 的直接子控件中的按钮 | |

**伪状态**写在冒号后面，表示控件处于某种状态时才生效：

| 伪状态 | 含义 |
|---|---|
| `:hover` | 鼠标悬停 |
| `:pressed` | 按下 |
| `:focus` | 有键盘焦点 |
| `:disabled` / `:enabled` | 禁用 / 可用 |
| `:checked` | 选中（复选框、可切换的按钮） |

多条规则作用于同一个控件时，和 CSS 一样，**更具体的规则优先**：`QPushButton#primary` 比 `QPushButton` 具体，所以「登录」按钮的背景是蓝色。

### 2.3 盒模型

和 CSS 一样，每个控件是一个矩形的盒子，从外到内：外边距（margin）、边框（border）、内边距（padding）、内容。

```
┌─────────── margin ───────────┐
│ ┌───────── border ─────────┐ │
│ │ ┌─────── padding ──────┐ │ │
│ │ │       内容（文字）       │ │ │
│ │ └──────────────────────┘ │ │
│ └──────────────────────────┘ │
└──────────────────────────────┘
```

2.1 节的 `QLineEdit:focus` 中把边框从 1 像素改成 2 像素，同时把内边距各减 1。如果不减，获得焦点时边框变粗，文字会往里挤 1 像素，看起来像跳了一下。

### 2.4 动态属性不会自动刷新

常见的需求：输入不合格时，把输入框的边框变成红色。可以给控件设置一个**动态属性**（`QObject::setProperty` 可以给任何对象加一个任意名字的属性），在样式表中用属性选择器匹配它：

```cpp
app.setStyleSheet(R"(
    QLineEdit { border: 2px solid #c8c8cc; border-radius: 0; padding: 4px; background: white; }
    QLineEdit[error="true"] { border: 2px solid #ff3b30; background: #fff0f0; }
)");
```

验证错误时设置属性。为了客观地判断边框有没有变色，把输入框截图，读出左边框中间那个像素的颜色：

```cpp
// 取输入框左边框中间那个像素的颜色
QString borderColor(QWidget *w)
{
    QImage img = w->grab().toImage();
    return img.pixelColor(1, img.height() / 2).name();
}

std::printf("初始:                     边框 %s\n", qPrintable(borderColor(f.password)));
f.password->setProperty("error", true);
f.password->update();
QApplication::processEvents();
std::printf("setProperty + update():   边框 %s\n", qPrintable(borderColor(f.password)));
f.password->style()->unpolish(f.password);
f.password->style()->polish(f.password);
std::printf("unpolish + polish:        边框 %s\n", qPrintable(borderColor(f.password)));
```

```
初始:                     边框 #c8c8cc
setProperty + update():   边框 #c8c8cc
unpolish + polish:        边框 #ff3b30
```

属性改了，也重画了，边框仍然是灰色的 `#c8c8cc`。原因是样式表的规则不是每次绘制时重新匹配的，而是在控件被「抛光」（**polish**，第一章事件序列中的 `Polish` 事件）时算好、缓存起来的。之后属性再变，Qt 并不知道这个属性和样式表有关。`unpolish` + `polish` 让控件丢掉缓存，重新匹配一次，边框才变成红色 `#ff3b30`。

伪状态（`:focus`、`:hover`、`:disabled`）不需要这样做，Qt 会在状态变化时自动更新。只有自己加的动态属性才需要。可以写一个小函数：

```cpp
void setStyleProperty(QWidget *w, const char *name, const QVariant &value)
{
    w->setProperty(name, value);
    w->style()->unpolish(w);
    w->style()->polish(w);
}
```

### 2.5 样式表的开销

样式表很方便，但每个控件都要匹配一遍规则。一个窗口中放 2000 个按钮，比较不用样式表、一开始就设置样式表、显示之后再更换样式表三种情况，各运行两次：

```
none    2000 个按钮: 创建    1.7 ms，显示   92.8 ms，(再)设置样式表    0.2 ms
none    2000 个按钮: 创建    1.7 ms，显示   90.5 ms，(再)设置样式表    0.2 ms
app     2000 个按钮: 创建    6.2 ms，显示  101.5 ms，(再)设置样式表   52.3 ms
app     2000 个按钮: 创建    6.3 ms，显示  101.6 ms，(再)设置样式表   53.4 ms
change  2000 个按钮: 创建    1.7 ms，显示   91.8 ms，(再)设置样式表   39.2 ms
change  2000 个按钮: 创建    1.8 ms，显示   87.2 ms，(再)设置样式表   37.2 ms
```

- 有样式表时，创建控件慢了大约 3.6 倍（1.7 → 6.2 毫秒），显示慢了约 10%；
- **更换**整个程序的样式表，要对所有控件重新匹配，2000 个按钮用了 40～50 毫秒，界面会卡一下。

对一般的界面来说，这些开销都不大。需要注意的是：不要频繁地调用 `setStyleSheet`（比如在动画的每一帧中改样式表来改颜色），也不要给大量的控件**各自**设置样式表（每个控件一份，就要各自解析一份）。需要频繁变化的颜色，用第 4 节的调色板，或者第 4 篇自己绘制。

> 样式表还有一个副作用：给某种控件设置了边框、背景等属性后，Qt 就不再用平台原生的方式画它，而是改用样式表的方式画。所以样式表要么不写，要么就把这种控件的各种状态（悬停、按下、禁用、焦点）都写全，否则会出现「一部分原生、一部分自定义」的拼凑感。

## 3. 深色模式的问题

macOS、Windows、主流的 Linux 桌面都支持深色模式。Qt 6 会读取系统的设置，自动使用深色的调色板。程序可以通过 `QStyleHints` 查询，也可以强制设置（Qt 6.8 起）：

```cpp
QStyleHints *hints = QGuiApplication::styleHints();
hints->colorScheme();                               // Qt::ColorScheme::Light / Dark / Unknown
hints->setColorScheme(Qt::ColorScheme::Dark);       // 强制深色，Unknown 表示跟随系统
```

强制切换到深色，看看调色板的变化：

```
设置后:   浅色，窗口背景 #ffffff，文字 #000000
设置后:   深色，窗口背景 #1e1e1e，文字 #ffffff
```

不用样式表时，整个界面自动变成深色：

<img src="/img/client/scheme-dark-none.png" alt="不用样式表，深色模式下的登录框" width="340">

可是 2.1 节的样式表中写死了 `background: white`，在深色模式下：

<img src="/img/client/scheme-dark-hardcoded.png" alt="写死了白色背景的输入框在深色模式下：文字看不见" width="340">

输入框还是白色的，在深色的窗口中特别刺眼；更糟的是，**输入框里的文字不见了**：文字颜色没有写死，跟着调色板变成了白色，白底白字。用户以为输入框是空的。

这就是样式表和深色模式的冲突：样式表中写死的颜色不会跟着系统变，没写的颜色却会变，两者混在一起就出问题。

## 4. 调色板

### 4.1 调色板是什么

**调色板**（`QPalette`）是一组「有含义的颜色」，控件绘制时不直接用某个颜色，而是用调色板中某个**角色**的颜色：

| 角色 | 用途 | 浅色 | 深色 |
|---|---|---|---|
| `Window` | 窗口背景 | `#ffffff` | `#1e1e1e` |
| `WindowText` | 窗口上的文字 | `#000000` | `#ffffff` |
| `Base` | 输入框、列表的背景 | `#ffffff` | `#171717` |
| `Text` | 输入框、列表中的文字 | | |
| `Button` / `ButtonText` | 按钮的背景 / 文字 | | |
| `Highlight` / `HighlightedText` | 选中项的背景 / 文字 | | |
| `Mid`、`Dark`、`Light` | 边框、阴影等 | | |
| `PlaceholderText` | 输入框的占位文字 | | |

（表中的颜色是在实验的机器上打印出来的，空着的格子没有一一打印。）

系统切换深色模式时，Qt 换掉的是整个调色板，所有按角色取颜色的控件就自动变色了。上一篇的表格模型中，「在线」「离线」返回的是写死的绿色和灰色，它们在深色背景上也不一定好看，实际的程序中最好也换成调色板的颜色，或者根据当前的配色返回不同的颜色。

### 4.2 在样式表中引用调色板

样式表中可以用 `palette(角色)` 代替写死的颜色：

```css
QLineEdit {
    border: 1px solid palette(mid);
    border-radius: 6px;
    padding: 5px 8px;
    background: palette(base);
    color: palette(text);
}
```

<img src="/img/client/scheme-dark-palette.png" alt="用 palette() 的样式表在深色模式下：输入框是深色背景、浅色文字" width="340">

输入框用深色背景、浅色文字，和整个窗口协调了。所以写样式表时，**和「背景」「文字」有关的颜色都用 `palette()`**；品牌色（如主按钮的蓝色）、提示色（如错误的红色）可以写死，但要分别在浅色和深色背景上检查一遍对比度。

### 4.3 运行中切换

用户在程序运行时切换系统的深色模式，Qt 会发出 `QStyleHints::colorSchemeChanged` 信号，并更新调色板。用 `palette()` 写的样式表会跟着变吗？

先在浅色模式下设置样式表，300 毫秒后切换到深色，再读出输入框背景的像素颜色：

```cpp
hints->setColorScheme(Qt::ColorScheme::Light);
const QString qss = "QLineEdit { border: 1px solid palette(mid); padding: 5px;"
                    " background: palette(base); color: palette(text); }";
app.setStyleSheet(qss);
// ... 300 ms 后
hints->setColorScheme(Qt::ColorScheme::Dark);
```

```
浅色: 输入框背景 #ffffff，调色板 Base #ffffff
colorSchemeChanged → 深色
深色: 输入框背景 #ffffff，调色板 Base #171717
```

调色板已经是深色（`Base` 变成了 `#171717`），输入框却还是白色。和 2.4 节的原因一样：`palette(base)` 是在设置样式表、抛光控件时**解析成具体颜色**缓存起来的，之后调色板变了，它不会重新解析。

那就在切换时重新设置一次样式表：

```cpp
QObject::connect(hints, &QStyleHints::colorSchemeChanged, [&](Qt::ColorScheme s) {
    std::printf("  此时调色板 Base %s\n", qPrintable(QApplication::palette().color(QPalette::Base).name()));
    app.setStyleSheet(qss);           // 重新设置一次，让 palette() 按新的调色板解析
});
```

```
colorSchemeChanged → 深色
  此时调色板 Base #ffffff
深色: 输入框背景 #ffffff，调色板 Base #171717
```

还是白色。打印出来就明白了：**发出 `colorSchemeChanged` 时，调色板还没有更新**，仍然是浅色的 `#ffffff`，这时重新设置样式表，解析出来的还是浅色。

**方法一**：在信号中不立即设置，而是用 `QTimer::singleShot(0, ...)` 推迟到下一轮事件循环，那时调色板已经更新了：

```cpp
QObject::connect(hints, &QStyleHints::colorSchemeChanged, [&] {
    QTimer::singleShot(0, &app, [&] { app.setStyleSheet(qss); });   // 等调色板更新完，再重新设置
});
```

```
colorSchemeChanged → 深色
  此时调色板 Base #ffffff
  稍后调色板 Base #171717，重新设置样式表
深色: 输入框背景 #171717，调色板 Base #171717
```

**方法二**：不依赖先后顺序，直接在调色板变化时重设：给 `QApplication` 装一个事件过滤器，处理 `QEvent::ApplicationPaletteChange` 事件。

```cpp
bool eventFilter(QObject *obj, QEvent *e) override
{
    if (obj == qApp && e->type() == QEvent::ApplicationPaletteChange) {
        std::printf("  ApplicationPaletteChange，Base %s，重新设置样式表\n", ...);
        qApp->setStyleSheet(m_qss);
    }
    return false;
}
```

```
colorSchemeChanged → 深色
  此时调色板 Base #ffffff
  ApplicationPaletteChange，Base #171717，重新设置样式表
  ApplicationPaletteChange，Base #171717，重新设置样式表
  ApplicationPaletteChange，Base #171717，重新设置样式表
  ……（共 15 次）
深色: 输入框背景 #171717，调色板 Base #171717
```

结果是对的，但重设了 15 次（直到实验结束）：**设置样式表本身又会引起调色板变化**，事件过滤器又重设一次样式表……形成了一个循环。按 2.5 节的测量，每重设一次就是几十毫秒，界面会明显卡顿。和上一章信号与槽的无限循环一样，解决办法是「值没变就不处理」：

```cpp
bool eventFilter(QObject *obj, QEvent *e) override
{
    if (obj == qApp && e->type() == QEvent::ApplicationPaletteChange) {
        QColor base = QApplication::palette().color(QPalette::Base);
        if (base == m_lastBase)
            return false;                   // 样式表自己引起的变化，忽略
        m_lastBase = base;
        qApp->setStyleSheet(m_qss);
    }
    return false;
}
```

```
colorSchemeChanged → 深色
  此时调色板 Base #ffffff
  ApplicationPaletteChange，Base #171717，重新设置样式表
深色: 输入框背景 #171717，调色板 Base #171717
```

只重设了一次，结果正确。

> 第 1 节说的另一种方式，直接修改调色板（`setPalette`），没有这些问题：控件每次绘制时都从调色板取颜色，调色板一变，下一次绘制就是新的颜色。只改颜色、不改形状（圆角、边框粗细）时，用调色板比用样式表更简单、更快。

## 5. 小结

- 样式表的语法和 CSS 一样：选择器（类名、`#对象名`、`[属性]`、后代）、伪状态（`:hover`、`:focus`、`:disabled`）、盒模型（margin、border、padding）。更具体的规则优先。
- 样式表在控件抛光时解析并缓存。修改动态属性后要 `unpolish` + `polish`，否则不会生效。
- 样式表有开销：创建控件慢几倍，更换整个程序的样式表要对所有控件重新匹配。不要频繁调用 `setStyleSheet`。
- 深色模式下，样式表中写死的颜色不会变，没写的颜色会跟着调色板变，两者混用会出现白底白字。背景和文字的颜色用 `palette(角色)`。
- `palette()` 也是在设置样式表时解析的；运行中切换深色模式时要重新设置样式表，而且要在调色板更新**之后**，并防止「设置样式表 → 调色板变化 → 再设置样式表」的循环。
- 只改颜色时，直接用调色板更简单。

样式表能改的只是现有控件的外观。如果要一个 Qt 没有的控件，比如一个开关、一个圆形的进度环、一个波形图，就要自己画了。下一篇讲 `QPainter` 和自定义控件。

## 练习

1. 给 2.1 节的样式表加上 `QPushButton:pressed` 和 `QPushButton#primary:hover`、`:pressed` 的规则，让按钮在按下时颜色更深。
2. 在登录对话框中，用户名不合格时调用 2.4 节的 `setStyleProperty` 把输入框标红，合格时恢复。把它连接到上一篇登录框的 `textChanged`。
3. 把 2.1 节样式表中所有写死的颜色分成两类：哪些应该换成 `palette()`，哪些可以保留？改完后分别在浅色和深色模式下截图对比。
4. 不用样式表，只用 `QPalette` 把「登录」按钮改成蓝底白字（提示：`QPalette::Button` 和 `QPalette::ButtonText`）。在 macOS 上效果如何？换成 Fusion 样式（`QApplication::setStyle("Fusion")`）再试试。
