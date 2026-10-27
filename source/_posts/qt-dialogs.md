---
title: 对话框与输入校验
date: 2026-10-28 09:00:00
updated: 2026-10-28 09:00:00
categories:
  - 客户端开发
  - 界面开发
tags:
  - Qt
  - C++
  - 对话框
order: 1
description: 对话框的 exec() 会一直等到用户关掉它才返回，可是在这期间，主窗口的定时器照样每 200 毫秒触发一次：exec() 里面运行着另一个事件循环。这个嵌套的事件循环正是 exec() 危险的地方：对话框打开的 0.3 秒里，它的父窗口被别处删除了，exec() 返回后再访问对话框，程序崩溃，退出码 139；换成 open() 加信号，就没有这个问题。本文还会写一个登录对话框：用正则校验器挡住数字开头、减号和中文，用户名和密码都合格时「登录」按钮才可用；实测 QIntValidator 允许输入 65536 和 99999，因为它们只是「还没输完」。最后给 QMessageBox 加载 Qt 自带的中文翻译，把 Cancel、Don't Save 变成「取消」「不保存」。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会写自己的对话框类，会用 `QDialogButtonBox` 放置标准按钮；理解 `exec()`、`open()`、`show()` 三种显示方式的区别，知道 `exec()` 中的嵌套事件循环和它带来的风险；会用 `QMessageBox`、`QFileDialog` 等标准对话框；会用校验器限制输入，并根据输入是否合格启用按钮；会加载 Qt 自带的翻译。
>
> **前置知识**：第一章，特别是《环境与第一个 Qt 程序》的事件循环和《对象树与内存管理》的悬空指针。
>
> **实验环境**：macOS 27.0，Qt 6.12.0，Apple Clang 21。

## 1. 三种显示方式

对话框（`QDialog`）是一个用来问用户一件事的窗口：登录、设置、确认删除、选一个文件。用户回答后对话框关闭，程序拿到结果继续执行。

`QDialog` 有三种显示方式：

| 方式 | 是否阻塞调用者 | 用户能否操作其他窗口 | 结果怎样拿到 |
|---|---|---|---|
| `exec()` | 阻塞，直到对话框关闭才返回 | 不能（应用程序模态） | `exec()` 的返回值 |
| `open()` | 立即返回 | 不能操作它的父窗口（窗口模态） | `finished(int)` / `accepted()` / `rejected()` 信号 |
| `show()` | 立即返回 | 能（非模态），例如「查找和替换」 | 信号 |

**模态**（modal）是指对话框打开期间，用户不能操作其他窗口。`accept()` 和 `reject()` 关闭对话框，并把结果设成 `QDialog::Accepted`（1）或 `QDialog::Rejected`（0）。按 Esc 或点窗口的关闭按钮相当于 `reject()`。

`exec()` 写起来最顺手：

```cpp
LoginDialog dialog(this);
if (dialog.exec() == QDialog::Accepted)
    connectToServer(dialog.user());
```

看起来就像调用了一个普通的函数。但第一章说过，界面上的一切都靠事件循环推动，`exec()` 卡在这里不返回，主窗口的事件由谁来处理？

## 2. exec() 中的嵌套事件循环

### 2.1 主窗口的定时器照常触发

主窗口中有一个每 200 毫秒触发一次的定时器。300 毫秒时打开一个对话框，再用另一个定时器模拟用户在 0.7 秒后点「确定」：

```cpp
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QTimer>
#include <cstdio>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QElapsedTimer clock;
    clock.start();

    QTimer ticker;                       // 主窗口中一个每 200 ms 触发的定时器
    QObject::connect(&ticker, &QTimer::timeout, [&] {
        std::printf("%5lld ms  定时器触发\n", clock.elapsed());
    });
    ticker.start(200);

    QTimer::singleShot(300, [&] {
        QDialog dialog;
        dialog.setWindowTitle("设置");
        QTimer::singleShot(700, &dialog, &QDialog::accept);   // 模拟用户 0.7 秒后点「确定」
        std::printf("%5lld ms  调用 dialog.exec()\n", clock.elapsed());
        int r = dialog.exec();
        std::printf("%5lld ms  exec() 返回 %d（Accepted = %d）\n", clock.elapsed(), r, QDialog::Accepted);
    });
    QTimer::singleShot(1300, &app, &QApplication::quit);
    return app.exec();
}
```

```
  191 ms  定时器触发
  300 ms  调用 dialog.exec()
  407 ms  定时器触发
  570 ms  定时器触发
  760 ms  定时器触发
  950 ms  定时器触发
 1039 ms  exec() 返回 1（Accepted = 1）
```

`exec()` 从 300 毫秒一直阻塞到 1039 毫秒，这期间定时器照样触发了 4 次。（时间不是严格的 200 毫秒整数倍，因为 `QTimer` 默认是「粗略」定时器，允许有 5% 左右的误差，好让系统合并唤醒、省电；打开窗口本身也要花一点时间。）

原因是 `exec()` 内部又运行了一个事件循环，和 `app.exec()` 是同一种东西。调用栈是这样的：

```
main()
└── app.exec()                         ← 外层事件循环
    └── 300 ms 的定时器 → lambda
        └── dialog.exec()              ← 内层事件循环：取事件、分发事件
            ├── 定时器 → 打印「定时器触发」
            ├── 鼠标、键盘 → 对话框（发给其他窗口的输入被挡掉）
            ├── 绘制事件 → 所有窗口都照常重画
            └── 1000 ms：accept() → 内层循环退出，exec() 返回
```

所以 `exec()` 阻塞的只是「调用它的这个函数」，整个程序并没有停下来：窗口照常重画，定时器、网络数据照常处理，只是用户的输入只能发给对话框。`QMessageBox::question()`、`QFileDialog::getOpenFileName()` 这些「一行代码弹出对话框并返回结果」的函数，内部都是这样实现的。

### 2.2 嵌套事件循环的风险

既然内层事件循环会处理所有的事件，那么在 `exec()` 返回之前，程序中**任何事情都可能发生**，包括删除了调用 `exec()` 的那个对象。

主窗口 `window` 打开一个以它为父对象的对话框。对话框打开期间，`window` 被别处删除了，比如网络断开时程序关掉了整个页面：

```cpp
auto *window = new QWidget;
window->show();

QTimer::singleShot(200, [=] {
    auto *dialog = new QDialog(window);      // 对话框是 window 的子对象
    // 0.3 秒后，window 被别处删除了（例如网络断开，整个页面被关掉）
    QTimer::singleShot(300, [=] { std::printf("删除 window\n"); delete window; });

    int r = dialog->exec();                  // 嵌套事件循环，window 在这期间被删除
    std::printf("exec() 返回 %d，接着访问 dialog...\n", r);
    dialog->setWindowTitle("结果");           // dialog 已经随 window 一起被删除
    std::printf("访问完毕\n");
});
```

```
删除 window
exec() 返回 0，接着访问 dialog...
```

```bash
echo $?
```

```
139
```

`delete window` 是在内层事件循环中执行的，按对象树的规则，它把子对象 `dialog` 也删除了。对话框没了，内层循环结束，`exec()` 返回 0。可是 `exec()` 之后的代码并不知道这些，接着访问 `dialog`，就是访问已经释放的内存。用 ASan 运行，错误指向的正是 `exec()` 后面的那一行：

```
==59728==ERROR: AddressSanitizer: SEGV on unknown address 0x000000015462 ...
    #4 0x0001041fdfb8 in main::$_0::operator()() const openvsexec.cpp:33
```

在真实的程序中，`exec()` 往往写在某个窗口的成员函数中，`exec()` 返回后访问的是 `this` 的成员。如果 `this` 在对话框打开期间被删除了，就是这个错误，而且很难重现：只有用户恰好在对话框打开时遇到网络断开之类的情况才会发生。

### 2.3 用 open() 代替

把 `exec()` 换成 `open()`，结果通过信号拿到：

```cpp
    QObject::connect(dialog, &QDialog::finished, [](int r) {
        std::printf("finished(%d)\n", r);
    });
    dialog->open();                          // 立即返回
    std::printf("open() 返回了\n");
```

```
open() 返回了
删除 window
```

程序正常结束。`open()` 立即返回，没有嵌套的事件循环，也就没有「返回之后」的代码。`window` 被删除时 `dialog` 一起被删除，`finished` 不会再发出，lambda 不会被调用。处理结果的代码写在槽中，如果槽用到了某个对象，按上一章的习惯把它作为上下文对象传给 `connect`，对象删除时连接自动断开。

Qt 的官方文档也建议尽量用 `open()` 而不是 `exec()`。实际开发中可以这样取舍：

- 简单的、对话框打开期间不会有其他事情发生的场景（比如程序刚启动时的登录框、一个纯本地的确认框），`exec()` 写起来简单，可以用；
- 程序中有网络、定时器、其他线程的回调，对话框打开期间它们可能删除对象的，用 `open()` 加信号。

## 3. 写一个登录对话框

把上一章的登录框改写成一个对话框类，加上输入校验。

### 3.1 QDialogButtonBox

对话框底部的「确定」「取消」，用 `QDialogButtonBox` 放置：

```cpp
m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
m_buttons->button(QDialogButtonBox::Ok)->setText("登录");
connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
```

它的好处是按**平台的习惯**排列按钮：macOS 上「确定」在最右边，Windows 上「确定」在「取消」的左边。不必自己针对每个平台调整顺序。

### 3.2 完整的代码

```cpp
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

class LoginDialog : public QDialog
{
public:
    explicit LoginDialog(QWidget *parent = nullptr) : QDialog(parent)
    {
        setWindowTitle("登录");
        m_user = new QLineEdit;
        m_user->setPlaceholderText("字母开头，3～16 位字母、数字或下划线");
        m_user->setValidator(new QRegularExpressionValidator(
            QRegularExpression("[A-Za-z][A-Za-z0-9_]{0,15}"), m_user));
        m_password = new QLineEdit;
        m_password->setEchoMode(QLineEdit::Password);

        auto *form = new QFormLayout;
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->addRow("用户名", m_user);
        form->addRow("密码", m_password);

        m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        m_buttons->button(QDialogButtonBox::Ok)->setText("登录");
        connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        auto *root = new QVBoxLayout(this);
        root->addLayout(form);
        root->addWidget(m_buttons);

        connect(m_user, &QLineEdit::textChanged, this, &LoginDialog::updateOk);
        connect(m_password, &QLineEdit::textChanged, this, &LoginDialog::updateOk);
        updateOk();
    }
    QString user() const { return m_user->text(); }

private:
    void updateOk()
    {
        bool ok = m_user->hasAcceptableInput() && m_user->text().size() >= 3
                  && m_password->text().size() >= 8;
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(ok);
    }
    QLineEdit *m_user, *m_password;
    QDialogButtonBox *m_buttons;
};
```

这个类没有自定义信号和槽，所以不需要 `Q_OBJECT`；`updateOk` 是普通的私有成员函数，第一章说过，任何成员函数都可以作为槽连接。

<img src="/img/client/login-dialog.png" alt="登录对话框：输入不合格时「登录」按钮不可用" width="380">

两个输入框任何一个的内容变化，都会调用 `updateOk()` 重新判断，用户名和密码都合格时「登录」按钮才可用。这比「点了登录再弹框说用户名不对」友好得多。

### 3.3 校验器挡住了什么

**校验器**（validator）在用户输入的每一个字符时检查一次，不合格的字符根本输入不进去。这里用正则表达式：第一个字符是字母，后面最多 15 个字母、数字或下划线。

用程序模拟键盘，逐个字符地「键入」几个字符串（`QKeyEvent` 就是按键时系统发给输入框的事件），看看最后输入框里是什么：

```cpp
for (QString s : {"9abc", "ab-c", "abc_12", "这是中文", "a234567890123456789"}) {
    edit->clear();
    for (QChar ch : s) {
        QKeyEvent press(QEvent::KeyPress, 0, Qt::NoModifier, QString(ch));
        QApplication::sendEvent(edit, &press);
    }
    std::printf("键入 %s → 输入框中是 \"%s\"\n", ..., qPrintable(edit->text()));
}
```

```
键入 "9abc"                → 输入框中是 "abc"
键入 "ab-c"                → 输入框中是 "abc"
键入 "abc_12"              → 输入框中是 "abc_12"
键入 "这是中文"            → 输入框中是 ""
键入 "a234567890123456789" → 输入框中是 "a234567890123456"
```

- 开头的数字 `9`、中间的 `-`、所有的中文都被挡掉了；
- 超过 16 个字符后，后面的字符输不进去；
- 合法的 `abc_12` 原样保留。

用这种模拟按键的方法，不用手点就能测试界面的行为，后面的章节写界面的自动化测试时还会用到。

### 3.4 Intermediate：还没输完

校验器的检查结果有三种：

| 状态 | 含义 | 输入框的行为 |
|---|---|---|
| `Acceptable` | 合格 | 可以输入，`hasAcceptableInput()` 为真 |
| `Intermediate` | 现在不合格，但继续输入可能变得合格 | **可以输入**，`hasAcceptableInput()` 为假 |
| `Invalid` | 不合格，再怎么输也不可能合格 | 输入被拒绝 |

`Intermediate` 很容易被忽略。用一个端口号校验器 `QIntValidator(1, 65535)` 检查几个值：

```cpp
QIntValidator port(1, 65535);
for (QString s : {"", "8", "80", "8080", "65535", "65536", "99999", "0", "-1", "8a"}) {
    int pos = 0;
    std::printf("端口 %-8s %s\n", ..., name(port.validate(s, pos)));
}
```

```
端口 ""       Intermediate
端口 "8"      Acceptable
端口 "80"     Acceptable
端口 "8080"   Acceptable
端口 "65535"  Acceptable
端口 "65536"  Intermediate
端口 "99999"  Intermediate
端口 "0"      Intermediate
端口 "-1"     Invalid
端口 "8a"     Invalid
```

超出范围的 `65536`、`99999` 和 `0` 都是 `Intermediate`，也就是说，用户**可以**在输入框中输入它们。`QIntValidator` 的判断方式比较宽松：位数没有超过上限的数字都算「可能还没输完」。只有带字母、带负号（下限是正数时）的输入才是 `Invalid`。

所以**校验器只能挡住明显错误的字符，不能保证输入框中的内容一定合格**。点「确定」之前还要检查 `hasAcceptableInput()`，3.2 节的 `updateOk()` 就是这样做的。用户名的正则校验器也一样：只输入一个字母 `a` 时是合格的（正则允许 1 个字符），「至少 3 位」要在 `updateOk()` 中另外检查。

> 输入校验只是为了让界面好用，不能代替服务器端的检查。客户端的程序可以被修改，请求也可以绕过界面直接发送，服务器收到的数据都要重新校验（数据库专栏《用 C 访问 MySQL》中的 SQL 注入就是这样发生的）。

## 4. 标准对话框

常用的对话框 Qt 都已经写好了，大多数都提供一个静态函数，一行代码弹出并返回结果：

| 类 | 用途 | 常用的静态函数 |
|---|---|---|
| `QMessageBox` | 提示、警告、确认 | `information`、`warning`、`critical`、`question` |
| `QFileDialog` | 选择文件或目录 | `getOpenFileName`、`getSaveFileName`、`getExistingDirectory` |
| `QInputDialog` | 输入一个字符串、数字，或从列表中选一项 | `getText`、`getInt`、`getItem` |
| `QColorDialog`、`QFontDialog` | 选颜色、选字体 | `getColor`、`getFont` |
| `QProgressDialog` | 显示耗时操作的进度 | 第 3 章用到 |

这些静态函数内部用的都是 `exec()`，所以第 2 节的风险对它们同样存在。`QFileDialog` 在 macOS 和 Windows 上默认使用系统原生的文件对话框，外观和系统一致。

### 4.1 QMessageBox 与翻译

关闭一个有未保存修改的文档时，询问用户是否保存：

```cpp
QMessageBox box(QMessageBox::Question, "未保存的修改", "文档「笔记.txt」有未保存的修改。",
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
box.setInformativeText("关闭前要保存吗？");
box.setDefaultButton(QMessageBox::Save);
switch (box.exec()) {
case QMessageBox::Save:    save(); break;
case QMessageBox::Discard: break;
case QMessageBox::Cancel:  return;     // 不关闭
}
```

<img src="/img/client/msgbox-en.png" alt="没有加载翻译的 QMessageBox：按钮是英文" width="406">

文字是中文，按钮却是英文：Save、Cancel、Don't Save 这些标准按钮的文字是 Qt 提供的，默认是英文。Qt 自带了各种语言的翻译文件，在程序启动时加载即可：

```cpp
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QTranslator qtTr;
    QString dir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    if (qtTr.load(QLocale(QLocale::Chinese, QLocale::China), "qtbase", "_", dir))
        app.installTranslator(&qtTr);
    // ...
}
```

`QLibraryInfo::path(QLibraryInfo::TranslationsPath)` 是 Qt 安装时翻译文件所在的目录，在实验的机器上是 `/opt/homebrew/share/qt/translations`，其中的 `qtbase_zh_CN.qm` 就是 Qt 基础模块的简体中文翻译。实际的程序通常用 `QLocale::system()` 代替写死的中文，按用户系统的语言加载。

<img src="/img/client/msgbox.png" alt="加载翻译后的 QMessageBox：按钮变成了中文" width="366">

按钮变成了「不保存」「取消」「保存」。注意按钮的位置：「不保存」单独放在最左边，「取消」和「保存」在右边，这是 macOS 的习惯，在 Windows 上会是「保存」「不保存」「取消」从左到右排列。这又是 `QMessageBox`（和 3.1 节的 `QDialogButtonBox`）替我们处理的平台差异。

> 发布程序时，翻译文件也要一起带上，否则在别人的电脑上找不到它，按钮又会变回英文。程序自己的界面文字也可以用 `tr()` 包起来，用 Qt 的工具提取出来翻译成其他语言，这里不展开。

## 5. 小结

- 对话框有三种显示方式：`exec()` 阻塞调用者并返回结果，`open()` 立即返回、窗口模态、通过信号给出结果，`show()` 非模态。
- `exec()` 内部运行着一个嵌套的事件循环，程序的其他部分照常运行。所以 `exec()` 返回时，调用它的对象可能已经被删除了；有网络、定时器等异步事件的场景，用 `open()` 加信号代替。
- `QDialogButtonBox` 和 `QMessageBox` 按平台的习惯排列按钮。
- 校验器挡住不可能合格的输入（`Invalid`），但允许「还没输完」的输入（`Intermediate`），提交前还要检查 `hasAcceptableInput()`；根据输入是否合格启用按钮，比事后报错友好。
- Qt 标准控件的文字需要加载 Qt 自带的翻译文件才会显示成中文。

到这里，界面上的控件都还很少。下一篇要显示大量的数据：几十万行的列表和表格，看看为什么 `QListWidget` 在这种情况下不够用，以及 Qt 的 Model/View 是怎样解决的。

## 练习

1. 在 2.1 的程序中，在 `dialog.exec()` 打开期间再用定时器打开第二个对话框（也用 `exec()`），画出这时的调用栈。两个对话框关闭的顺序必须是先开后关吗？
2. 把 3.2 节的 `LoginDialog` 改成用 `open()` 显示，在 `accepted` 信号的槽中打印用户名。
3. 写一个「IP 地址」输入框：用 `QRegularExpressionValidator` 限制成四段数字和点。正则能保证每一段都不超过 255 吗？`setInputMask("000.000.000.000")` 又有什么效果？
4. 加载翻译时把 `QLocale::China` 换成 `QLocale::Taiwan`，`QMessageBox` 的按钮会变成什么？去翻译目录中找一找对应的文件。
