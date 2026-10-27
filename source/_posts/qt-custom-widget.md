---
title: 自定义控件与绘图
date: 2026-10-28 12:00:00
updated: 2026-10-28 12:00:00
categories:
  - 客户端开发
  - 界面开发
tags:
  - Qt
  - C++
  - QPainter
order: 4
description: Qt 没有现成的开关控件，就自己画一个：继承 QWidget，在 paintEvent 中用 QPainter 画一个圆角矩形和一个圆，在 mouseReleaseEvent 中切换状态，再用 QVariantAnimation 让滑块用 150 毫秒滑过去，这期间开关重画了 12 次。放大 8 倍对比抗锯齿：关掉时边缘是锯齿状的台阶，打开后边缘有一圈半透明的过渡像素。实测 update() 和 repaint() 的区别：连续调用 1000 次 update()，只重画了 1 次，总共 40 毫秒；调用 1000 次 repaint()，真的重画了 1000 次，在屏幕上用了 15 秒。两次 update() 不同的小矩形，也被合并成了一次重画。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会继承 `QWidget` 写一个自定义控件：在 `paintEvent` 中用 `QPainter` 绘制，处理鼠标事件，提供 `sizeHint`，发出自己的信号；会用 `QVariantAnimation` 做简单的动画；理解抗锯齿；理解 `update()` 和 `repaint()` 的区别，知道 Qt 怎样合并重画请求；知道绘制时应该从调色板取颜色，以适应深色模式和禁用状态。
>
> **前置知识**：本章前三篇；第一章的事件循环和信号与槽。
>
> **实验环境**：macOS 27.0（Retina 屏，`devicePixelRatio` 为 2），Qt 6.12.0，Release 构建。

## 1. 什么时候需要自定义控件

Qt 的控件很多，但不是什么都有。手机上常见的开关、圆形的进度环、音乐播放器的波形、聊天窗口中的气泡，Qt 都没有现成的。样式表能做的也有限：它能改颜色和圆角，不能让一个复选框变成一个会滑动的开关。

这时就要自己画。Qt 中所有的控件，包括按钮和输入框，本质上都是这样实现的：继承 `QWidget`，在 `paintEvent` 中把自己画出来，在鼠标、键盘事件中改变状态。

本文写一个开关控件 `Switch`：

- 外观：一个圆角的轨道，里面一个白色的圆形滑块；关的时候轨道是灰色、滑块在左边，开的时候轨道是绿色、滑块在右边；
- 交互：点击切换，滑块有滑动的动画；
- 接口：`isChecked()`、`setChecked()`，状态变化时发出 `toggled(bool)` 信号；
- 禁用时变成浅灰色，点击无效。

## 2. 类的声明

```cpp
#pragma once
#include <QWidget>

class QVariantAnimation;

// 一个开关：点击在「开」和「关」之间切换，滑块有动画
class Switch : public QWidget
{
    Q_OBJECT

public:
    explicit Switch(QWidget *parent = nullptr);
    bool isChecked() const { return m_checked; }
    void setChecked(bool checked);
    QSize sizeHint() const override { return {44, 26}; }

signals:
    void toggled(bool checked);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    bool m_checked = false;
    qreal m_pos = 0;                 // 滑块位置：0 在左，1 在右
    QVariantAnimation *m_anim;
};
```

自定义控件就是重写 `QWidget` 的几个**虚函数**。第一章说过，所有的事件最终都会分发到控件上：鼠标点击、需要重画、大小改变，`QWidget::event()` 收到事件后，按事件类型调用对应的虚函数：

| 事件 | 虚函数 |
|---|---|
| `Paint` | `paintEvent(QPaintEvent *)` |
| `MouseButtonPress` / `Release` / `Move` | `mousePressEvent`、`mouseReleaseEvent`、`mouseMoveEvent` |
| `KeyPress` | `keyPressEvent` |
| `Resize` | `resizeEvent` |
| `Enter` / `Leave` | `enterEvent`、`leaveEvent`（鼠标进入、离开） |

`sizeHint` 也是虚函数，返回建议大小，第 1.2 篇《窗口、控件与布局》讲过布局怎样使用它。这里返回 44×26 的逻辑像素。

开关有一个自定义的信号 `toggled`，所以要写 `Q_OBJECT`，声明放在头文件中。

## 3. 绘制：paintEvent 和 QPainter

```cpp
void Switch::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 轨道：关是灰色，开是绿色，动画过程中两种颜色按位置混合
    QColor off = palette().color(QPalette::Mid), on("#34c759");
    QColor track = QColor::fromRgbF(off.redF() + (on.redF() - off.redF()) * m_pos,
                                    off.greenF() + (on.greenF() - off.greenF()) * m_pos,
                                    off.blueF() + (on.blueF() - off.blueF()) * m_pos);
    qreal h = height(), r = h / 2;
    p.setPen(Qt::NoPen);
    p.setBrush(isEnabled() ? track : off.lighter(130));
    p.drawRoundedRect(rect(), r, r);

    // 滑块：白色的圆，从左滑到右
    qreal d = h - 4;
    qreal x = 2 + (width() - d - 4) * m_pos;
    p.setBrush(Qt::white);
    p.drawEllipse(QRectF(x, 2, d, d));
}
```

**`QPainter`** 是 Qt 的绘图工具，所有的控件都用它画自己。在 `paintEvent` 中创建一个以 `this` 为目标的 `QPainter`，函数结束时它析构，绘制完成。常用的操作：

| 函数 | 作用 |
|---|---|
| `setPen(QPen)` | 设置画线的笔：颜色、粗细、虚线。`Qt::NoPen` 表示不画边线 |
| `setBrush(QBrush)` | 设置填充：颜色、渐变、图案。`Qt::NoBrush` 表示不填充 |
| `drawLine`、`drawRect`、`drawRoundedRect`、`drawEllipse`、`drawPath` | 画线和各种形状 |
| `drawText(矩形, 对齐, 文字)` | 画文字 |
| `drawPixmap`、`drawImage` | 画图片 |
| `save()` / `restore()` | 保存 / 恢复画笔、画刷、坐标变换等状态 |
| `translate`、`rotate`、`scale` | 坐标变换 |

坐标以控件左上角为原点，单位是逻辑像素，和 `setGeometry` 一样。在 Retina 屏上，Qt 自动把所有坐标乘以 2（打印 `devicePixelRatioF()` 得到 2），画出来的线条和文字都是清晰的，代码中不用关心物理像素。

几个值得注意的地方：

- **所有颜色都在 `paintEvent` 中现场决定**：开关的颜色取决于 `m_checked`（通过 `m_pos`）、`isEnabled()`，而不是保存在什么地方。状态变了，只要请求重画，下一次 `paintEvent` 自然就画出新的样子。这是自定义控件最基本的写法：**`paintEvent` 只根据当前的状态绘制，不改变状态**。
- **「关」的颜色从调色板取**（`QPalette::Mid`），上一篇说过，这样它在深色模式下会自动变成合适的颜色。绿色是品牌色，写死。
- **禁用时用更浅的颜色**，用户一眼就能看出它不能点。Qt 自己的控件都是这样做的，自定义控件也要处理 `isEnabled()`。

## 4. 交互和动画

### 4.1 鼠标事件

```cpp
void Switch::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
        setChecked(!m_checked);
}
```

在鼠标**松开**时切换，而不是按下时：用户按下后如果改变主意，把鼠标移出控件再松开，就不会切换。这也是 Qt 按钮的行为（`clicked` 是在松开时发出的）。`event->position()` 是鼠标相对于控件左上角的位置。

禁用的控件根本收不到鼠标事件，Qt 已经把它们挡掉了，所以这里不用检查 `isEnabled()`。

### 4.2 setChecked 和动画

```cpp
Switch::Switch(QWidget *parent) : QWidget(parent), m_anim(new QVariantAnimation(this))
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
    m_anim->setDuration(150);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_pos = v.toReal();
        update();                    // 每一帧都请求重画
    });
}

void Switch::setChecked(bool checked)
{
    if (checked == m_checked)
        return;
    m_checked = checked;
    m_anim->stop();
    m_anim->setStartValue(m_pos);
    m_anim->setEndValue(checked ? 1.0 : 0.0);
    m_anim->start();
    emit toggled(checked);
}
```

- **`QVariantAnimation`** 在指定的时间（150 毫秒）内，把一个值从起点变到终点，每一帧发出一次 `valueChanged`。`OutCubic` 是缓动曲线：开始快、结束慢，比匀速运动看起来更自然。动画也是由事件循环中的定时器驱动的。
- 动画的起点是**当前的** `m_pos`，而不是 0 或 1：用户快速连点两下，滑块会从半路折返，而不是跳回起点。
- `setChecked` 开头的「值没变就返回」，就是第一章信号与槽中防止无限循环的那一行。
- 动画对象以 `this` 为父对象，开关删除时它也被删除（对象树）；连接 lambda 时把 `this` 作为上下文对象。第一章的两条习惯，这里都用上了。

### 4.3 运行效果

把三个开关放进一个表单，第一个打开，第三个禁用：

```cpp
auto *wifi = new Switch, *bt = new Switch, *air = new Switch;
wifi->setChecked(true);
air->setEnabled(false);
form->addRow("Wi-Fi", wifi);
form->addRow("蓝牙", bt);
form->addRow("飞行模式（不可用）", air);
QObject::connect(bt, &Switch::toggled, [](bool on) { std::printf("蓝牙 toggled(%d)\n", on); });
```

<img src="/img/client/switch-form.png" alt="三个开关：Wi-Fi 打开，蓝牙关闭，飞行模式禁用" width="211">

然后用第 2.1 篇模拟按键的方法，向蓝牙开关发送一次鼠标按下和松开，同时用事件过滤器数一数它重画了几次：

```
蓝牙开关 sizeHint 44x26，devicePixelRatio 2
蓝牙 toggled(1)
150 ms 的动画中，蓝牙开关重画了 12 次
```

<img src="/img/client/switch-after.png" alt="点击之后，蓝牙开关打开" width="211">

150 毫秒重画了 12 次，大约每 12.5 毫秒一次，每秒 80 帧左右。动画每一帧调用一次 `update()`，Qt 再根据屏幕的刷新节奏安排实际的重画。

## 5. 抗锯齿

第 3 节的 `paintEvent` 开头有一行 `p.setRenderHint(QPainter::Antialiasing)`。把同一个开关的形状画在一张小图片上，左边关掉抗锯齿，右边打开，放大 8 倍看像素：

```cpp
QImage img(100, 30, QImage::Format_ARGB32);
img.fill(Qt::white);
QPainter p(&img);                            // QPainter 也可以画在图片上
p.setPen(Qt::NoPen);
p.setBrush(QColor("#34c759"));
for (int k = 0; k < 2; ++k) {
    p.setRenderHint(QPainter::Antialiasing, k == 1);
    p.drawRoundedRect(QRectF(4 + k * 50, 3, 42, 24), 12, 12);
}
// ... 同样画两个白色的圆
img.scaled(800, 240, Qt::IgnoreAspectRatio, Qt::FastTransformation).save("antialias.png");
```

<img src="/img/client/antialias.png" alt="放大 8 倍：左边不抗锯齿，边缘是台阶；右边抗锯齿，边缘有过渡色" width="400">

屏幕由一个个方形的像素组成，斜线和曲线经过的像素，只能「涂」或者「不涂」。不抗锯齿时（左），边缘是一级一级的台阶。抗锯齿时（右），曲线只盖住一部分的像素会被涂成**半透明**的颜色，盖住得越多颜色越深，放大看边缘是一圈过渡色，正常大小看起来就是光滑的曲线。

抗锯齿要计算每个边缘像素被覆盖了多少，比不抗锯齿慢。画圆、圆角、斜线时都应该打开；只画横平竖直的线和矩形时（例如表格的网格线）可以不开，开了反而可能让 1 像素的线变成 2 个半透明的像素，看起来发虚。

> 同样是 `QPainter`，可以画在控件上、`QImage`（内存中的图片，可以逐像素读写）、`QPixmap`（为屏幕显示优化的图片），甚至打印机和 PDF 上（`QPdfWriter`）。前面几篇用 `grab()` 截图，就是让控件把自己画在一个 `QPixmap` 上。

## 6. update() 和 repaint()

动画中每一帧调用的是 `update()`。Qt 还有一个 `repaint()`，两者都是「请求重画」，区别是：

- **`update()`**：往事件队列中放一个重画请求，立即返回。回到事件循环后再重画。
- **`repaint()`**：立即调用 `paintEvent`，画完才返回。

区别有多大？写一个每次重画都要画 2000 条线的控件，分别连续调用 1000 次 `update()` 和 1000 次 `repaint()`：

```cpp
c.paints = 0;
t.start();
for (int i = 0; i < 1000; ++i)
    c.update();
double call = t.nsecsElapsed() / 1e6;
QApplication::processEvents();
std::printf("update()  × 1000: 调用用时 %7.1f ms，之后重画 %4d 次，总用时 %7.1f ms\n", ...);

c.paints = 0;
t.start();
for (int i = 0; i < 1000; ++i)
    c.repaint();
std::printf("repaint() × 1000: 调用用时 %7.1f ms，重画 %4d 次\n", ...);
```

在屏幕上运行（`cocoa` 平台插件）：

```
update()  × 1000: 调用用时     0.0 ms，之后重画    1 次，总用时   40.2 ms
repaint() × 1000: 调用用时 15193.5 ms，重画 1000 次
```

用 `offscreen` 平台插件运行（只在内存中画，不送到屏幕）：

```
update()  × 1000: 调用用时     0.0 ms，之后重画    1 次，总用时    3.7 ms
repaint() × 1000: 调用用时  2793.2 ms，重画 1000 次
```

- 1000 次 `update()` 只引起了 **1 次**重画。重画请求在队列中会被**合并**：控件已经有一个还没处理的重画请求时，再调用 `update()` 什么也不做。
- 1000 次 `repaint()` 真的画了 1000 次。在内存中画，每次约 2.8 毫秒（画 2000 条线的时间）；在屏幕上，每次画完还要送到屏幕显示，平均 15 毫秒，一共 15 秒，这期间程序完全没有反应。

所以**几乎总是用 `update()`**。它天然地避免了无用的重画：数据在一次事件处理中改了 100 次，界面只画最后的结果一次。`repaint()` 只用在极少数「必须马上看到」的场合，而且这种场合往往说明代码结构有问题，比如在界面线程中做了一个长循环，想在循环中途刷新进度，正确的做法是把循环放到另一个线程（第 3 章）。

### 6.1 只重画一部分

`update()` 可以传入一个矩形，只请求重画控件的一部分：

```cpp
c.update(QRect(10, 10, 20, 20));
c.update(QRect(300, 200, 30, 30));
```

```
update 两个小矩形: 重画 1 次，rect() 320x220+10+10，region() 由 2 个矩形组成
```

两个请求同样被合并成了一次重画。`paintEvent` 收到的 `QPaintEvent` 中，`region()` 是需要重画的区域（这里正好是两个小矩形），`rect()` 是包住它们的外接矩形。Qt 在调用 `paintEvent` 之前已经设置好了**裁剪**：画到这个区域之外的内容不会出现在屏幕上。

所以 `paintEvent` 中画了整个控件也不会出错，只是浪费了时间。对于复杂的控件（几千个点的图表、很大的画布），可以只画和 `event->rect()` 相交的部分，Qt 自己的视图就是这样做的：上一篇的 `QListView` 只画了可见区域中的 24 行。

## 7. 小结

- 自定义控件就是继承 `QWidget`，重写事件处理的虚函数：`paintEvent` 绘制、`mouse…Event` 处理鼠标、`sizeHint` 提供建议大小，需要时发出自己的信号。
- `paintEvent` 中用 `QPainter` 绘制：笔画线、画刷填充、各种形状和文字。坐标是逻辑像素，高分辨率屏由 Qt 自动处理。
- `paintEvent` 只根据当前状态绘制；状态变化时调用 `update()` 请求重画。颜色从调色板取，并处理禁用状态。
- 动画用 `QVariantAnimation`（或 `QPropertyAnimation`），每一帧更新状态并 `update()`。
- 画曲线和斜线时打开抗锯齿。
- `update()` 把重画请求放进队列，多次请求会合并成一次；`repaint()` 立即重画，连续调用代价很高。几乎总是用 `update()`。

第二章到这里结束：会用对话框和输入校验，会用 Model/View 显示大量数据，会用样式表和调色板改变外观、适应深色模式，也会自己画控件。这一章反复遇到同一个限制：界面上的一切都在一个线程中、由事件循环推动，任何耗时的操作（排序 100 万行、重画 1000 次）都会让界面卡住。下一章专门解决这个问题：事件循环的细节、事件过滤器，以及怎样把耗时的工作放到别的线程中去。

## 练习

1. 给 `Switch` 加上键盘支持：控件获得焦点时（`setFocusPolicy(Qt::StrongFocus)`），按空格键切换；有焦点时画一圈焦点框。
2. 把动画改成 `QPropertyAnimation`：用 `Q_PROPERTY(qreal pos READ pos WRITE setPos)` 声明一个属性，动画直接修改这个属性。和 `QVariantAnimation` 相比，代码有什么不同？
3. 写一个圆形的进度环控件 `ProgressRing`，用 `drawArc` 画进度，中间用 `drawText` 显示百分比。进度从 0 变到 100 时，用 `update()` 而不是 `repaint()`，并验证 100 次 `setValue` 只引起很少几次重画。
4. 在第 6 节的实验中，把每次画 2000 条线改成只画和 `event->rect()` 相交的线（提示：先算出线段的外接矩形，用 `intersects` 判断）。重新运行「两个小矩形」的实验，`paintEvent` 的耗时减少了多少？
