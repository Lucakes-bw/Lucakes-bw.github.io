---
title: 实战：主窗口与聊天气泡
date: 2026-10-31 10:00:00
updated: 2026-10-31 10:00:00
categories:
  - 客户端开发
  - 综合实战
tags:
  - Qt
  - C++
  - Model/View
  - 委托
order: 2
description: 客户端的界面：左边是联系人列表，右边是聊天记录和输入框。联系人用一个 QAbstractListModel，圆点表示在线状态，未读的消息让名字变粗并显示数量；聊天记录也是一个模型，配上自己写的委托，把每条消息画成气泡：自己的在右边、蓝色，对方的在左边、灰色，长消息自动换行，还没确认的消息颜色变浅、显示「发送中…」。气泡的高度各不相同，不能用 uniformItemSizes，实测每条消息的布局约 12 微秒：200 条 2.5 毫秒，2 万条 240 毫秒，而且窗口每改变一次宽度都要全部重新计算，所以一次只加载最近的 200 条。开发中还遇到一个问题：重连后用 beginResetModel 刷新联系人，选中的联系人和未读数全丢了，改成逐个更新后才正常。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：看懂 QtChat 客户端的整体结构：网络、协议、存储、模型、界面各由哪个类负责；会用 `QStyledItemDelegate` 画高度不固定的列表项，理解 `sizeHint` 和 `paint` 的分工；理解为什么聊天记录不能用 `uniformItemSizes`，以及怎样控制一次加载的数量；会用事件过滤器实现「回车发送」；知道什么时候该重置模型、什么时候不该。
>
> **前置知识**：上一篇《实战：聊天协议与服务器》；第二章《对话框与输入校验》《Model/View：显示大量数据》《自定义控件与绘图》；第三章《事件的分发与传播》。
>
> **实验环境**：macOS 27.0，Qt 6.12.0，Release 构建。截图由两个客户端按脚本自动操作后用 `grab()` 保存（第 6 节），原图是 Retina 的两倍大小。

## 1. 整体结构

客户端有 8 个类，大约 1000 行代码：

```
main.cpp                       解析命令行，显示登录框，创建下面的对象
│
├── LoginDialog                登录对话框（用户名校验、记住上次的输入）
│
├── ChatClient                 协议：登录、发消息、处理确认和收到的消息
│   ├── Connection             一条 TCP 长连接：分帧、心跳、自动重连
│   └── MessageStore           本地的 SQLite 数据库
│
└── MainWindow                 主窗口
    ├── ContactModel  + QListView                    联系人
    ├── MessageModel  + QListView + BubbleDelegate   当前会话的消息
    └── QPlainTextEdit + QPushButton                 输入框和发送按钮
```

分层的原则是：**下层不知道上层的存在**。`Connection` 只管字节和帧，不知道什么是登录、什么是聊天消息；`ChatClient` 只管协议，不知道界面长什么样；界面通过信号得知发生了什么。这样，`Connection` 原样来自第四章的代码，`ChatClient` 可以脱离界面单独测试（上一篇就是用脚本测的服务器）。

`ChatClient` 对外提供的接口：

```cpp
class ChatClient : public QObject
{
    Q_OBJECT
public:
    bool start(const QString &user, const QString &host, quint16 port);
    void sendText(const QString &peer, const QString &text);

signals:
    void stateChanged(const QString &text, bool online);          // 状态栏的文字
    void loggedIn(const QList<QPair<QString, bool>> &users);      // 联系人和在线状态
    void presence(const QString &user, bool online);
    void messageAdded(const ChatMessage &m);                      // 新发出或者新收到的消息
    void statusChanged(qint64 msgId, ChatMessage::Status status); // 发送中 → 已发送 / 失败
    void kicked();
};
```

协议和存储的细节在下一篇，这一篇看界面。

## 2. 主窗口的布局

```cpp
MainWindow::MainWindow(ChatClient *client, QWidget *parent) : QMainWindow(parent), m_client(client)
{
    setWindowTitle("QtChat - " + client->user());

    // 左边：联系人
    m_contactView = new QListView;
    m_contactView->setModel(&m_contacts);
    m_contactView->setUniformItemSizes(true);              // 每行一样高（《Model/View》）
    m_contactView->setIconSize(QSize(10, 10));

    // 右边：标题、消息、输入框
    m_title = new QLabel("选择一个联系人开始聊天");
    // ……加粗

    m_messageView = new QListView;
    m_messageView->setModel(&m_messages);
    m_messageView->setItemDelegate(new BubbleDelegate(m_messageView));
    m_messageView->setSelectionMode(QAbstractItemView::NoSelection);
    m_messageView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_messageView->setResizeMode(QListView::Adjust);        // 宽度变了，重新计算每个气泡的高度
    m_messageView->setFrameShape(QFrame::NoFrame);

    m_input = new QPlainTextEdit;
    m_input->setPlaceholderText("输入消息，回车发送，Shift+回车换行");
    m_input->setMaximumHeight(72);
    m_input->installEventFilter(this);                      // 拦截回车键（《事件的分发与传播》）
    m_send = new QPushButton("发送");
    m_send->setEnabled(false);

    auto *inputRow = new QHBoxLayout;
    inputRow->addWidget(m_input);
    inputRow->addWidget(m_send, 0, Qt::AlignBottom);

    auto *right = new QWidget;
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->addWidget(m_title);
    rightLayout->addWidget(m_messageView, 1);              // 伸缩系数 1：多余的高度都给消息列表
    rightLayout->addLayout(inputRow);

    auto *splitter = new QSplitter;                         // 用户可以拖动中间的分隔条
    splitter->addWidget(m_contactView);
    splitter->addWidget(right);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({160, 440});
    setCentralWidget(splitter);
    // ……状态栏和信号连接
}
```

第二章学过的东西都用上了：布局和伸缩系数决定了窗口变大时，多出来的空间给消息列表；`QSplitter` 让用户可以拖动左右两栏的分界；输入框为空时「发送」按钮不可用（和第二章的登录框一样，用 `textChanged` 更新）。

几个列表视图的设置：

- 联系人每行一样高，打开 `uniformItemSizes`（第二章 2.2 篇，它把 100 万行的显示从 3.9 秒降到了 0.1 秒）。
- 消息列表**不能**打开它：每个气泡的高度取决于消息的长度。第 4 节会看到这带来的代价。
- `ScrollPerPixel`：默认的滚动单位是「一行」，气泡高度不一时，滚动会一跳一跳的。
- `setResizeMode(QListView::Adjust)`：视图的宽度改变时，重新计算每一项的大小。气泡最宽是视图宽度的 65%，窗口变宽，长消息换行的位置会变，高度也跟着变。

## 3. 气泡委托

### 3.1 委托是什么

第二章的视图，每一行的样子都是视图自己画的：文字、图标、颜色。想把一行画成任意的样子，要给视图设置一个**委托**（delegate）：视图需要画某一项、需要知道某一项多大时，都去问委托。

继承 `QStyledItemDelegate`，重写两个函数：

| 函数 | 视图什么时候调用 | 要做什么 |
|---|---|---|
| `sizeHint(option, index)` | 布局时，算每一项的位置和总高度 | 返回这一项需要的大小 |
| `paint(painter, option, index)` | 绘制时，只对可见的项 | 用 `QPainter` 把这一项画在 `option.rect` 中 |

`option` 里有视图传过来的信息：这一项的矩形 `rect`、字体 `fontMetrics`、调色板 `palette`、视图本身 `widget`。数据都通过 `index.data(角色)` 从模型中取，委托本身不保存任何消息。

### 3.2 sizeHint：先算出文字占多大

```cpp
static constexpr int kPadX = 12, kPadY = 8;     // 气泡内边距
static constexpr int kMargin = 12;              // 气泡离视图边缘的距离
static constexpr int kGap = 6;                  // 两条消息之间的距离
static constexpr int kMetaH = 16;               // 时间和状态那一行的高度

// 文字在气泡中占的矩形：最宽是视图宽度的 65%，超过就自动换行
QRect BubbleDelegate::textRect(const QStyleOptionViewItem &option, const QString &text) const
{
    int viewWidth = option.widget ? option.widget->width() : option.rect.width();
    int maxWidth = std::max(80, int(viewWidth * 0.65) - 2 * kPadX);
    return option.fontMetrics.boundingRect(QRect(0, 0, maxWidth, 100000),
                                           Qt::TextWordWrap, text);
}

QSize BubbleDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QRect r = textRect(option, index.data().toString());
    return {r.width() + 2 * kPadX, r.height() + 2 * kPadY + kMetaH + kGap};
}
```

`QFontMetrics::boundingRect` 在给定的宽度内、按 `Qt::TextWordWrap` 自动换行排版这段文字，返回它实际占用的矩形：短消息返回的宽度就是文字的宽度，长消息返回最大宽度、高度是若干行。气泡的大小就是文字的大小加上内边距，再加上下面一行时间的高度。

### 3.3 paint：画气泡

```cpp
void BubbleDelegate::paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const QString text = index.data().toString();
    const bool out = index.data(MessageModel::OutgoingRole).toBool();
    const auto status = ChatMessage::Status(index.data(MessageModel::StatusRole).toInt());

    QRect tr = textRect(option, text);
    QRect bubble(0, option.rect.top() + kGap / 2, tr.width() + 2 * kPadX, tr.height() + 2 * kPadY);
    if (out)
        bubble.moveRight(option.rect.right() - kMargin);
    else
        bubble.moveLeft(option.rect.left() + kMargin);

    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(bubble, 12, 12);
    QColor bg = out ? QColor("#0a84ff") : option.palette.color(QPalette::AlternateBase);
    if (out && status == ChatMessage::Sending)
        bg.setAlphaF(0.6);                      // 还没确认的消息颜色浅一些
    p->fillPath(path, bg);

    p->setPen(out ? QColor(Qt::white) : option.palette.color(QPalette::Text));
    p->drawText(bubble.adjusted(kPadX, kPadY, -kPadX, -kPadY), Qt::TextWordWrap, text);

    // 气泡下面一行小字：时间，自己的消息再加上状态
    QString meta = index.data(MessageModel::TimeRole).toString();
    if (out) {
        if (status == ChatMessage::Sending) meta += "  发送中…";
        else if (status == ChatMessage::Failed) meta += "  发送失败";
    }
    QFont small = option.font;
    small.setPointSizeF(small.pointSizeF() * 0.8);
    p->setFont(small);
    p->setPen(status == ChatMessage::Failed && out ? QColor("#ff3b30")
                                                   : option.palette.color(QPalette::PlaceholderText));
    // 自己的消息：和气泡右边对齐；对方的消息：和气泡左边对齐。文字比气泡宽时向外延伸
    int y = bubble.bottom() + 2;
    QRect metaRect = out ? QRect(QPoint(option.rect.left(), y), QPoint(bubble.right(), y + kMetaH))
                         : QRect(QPoint(bubble.left(), y), QPoint(option.rect.right(), y + kMetaH));
    p->drawText(metaRect, (out ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignTop, meta);
    p->restore();
}
```

- `paint` 中**重新算一遍**文字的矩形，而不是保存 `sizeHint` 的结果：委托不保存状态，同一个委托对象画所有的项。两处用同一个 `textRect` 函数，保证算出来的一样。
- 圆角矩形用 `QPainterPath` 加抗锯齿（第二章《自定义控件与绘图》第 5 节）。
- 对方气泡的背景和文字颜色取自调色板（`AlternateBase`、`Text`），深色模式下会自动变（第二章《样式表与深色模式》）；自己的气泡用品牌色蓝色，文字固定白色。
- `p->save()` / `p->restore()`：委托画完一项，要把画笔、字体恢复原样，否则会影响视图画下一项。

画出来的效果，左边是 lucky，下面是 zhangsan 看到的同一段对话：

<img src="/img/client/chat-lucky.png" alt="lucky 的窗口：自己的消息在右边，蓝色" width="640">

<img src="/img/client/chat-zhangsan.png" alt="zhangsan 的窗口：同一段对话，左右颠倒" width="640">

长的那条消息在 65% 的宽度处换了行，气泡的高度也随之变成了两行；每条消息下面是时间。联系人列表中，在线的人是绿点，离线的是灰点，lisi 发来了一条 lucky 还没看的消息，名字加粗并显示「（1）」。

## 4. 一次加载多少条消息

聊天记录的气泡高度各不相同，消息列表不能用 `uniformItemSizes`。第二章 2.2 篇说过，这时视图需要知道**每一行**的大小，才能算出滚动条和每一行的位置，也就是要对每一条消息调用一次 `sizeHint`。

用客户端真正的 `MessageModel` 和 `BubbleDelegate`，分别装入 200、2000、2 万条长短不一的消息，测量打开会话（装入数据、布局、滚到底部、画出第一屏）的时间，以及窗口改变宽度后重新布局的时间。每种情况测 3 次取最快的，并先预热一轮（第一次运行时要加载字体等，会慢很多）：

```cpp
model.load("peer", list);
view.doItemsLayout();                 // 立即计算所有行的大小和位置
view.scrollToBottom();
view.repaint();
// ……
view.resize(view.width() == 440 ? 600 : 440, 400);
view.doItemsLayout();                 // 宽度变了，气泡的换行变了，全部重新计算
view.repaint();
```

```
   200 条：打开会话     2.5 ms，改变窗口宽度后重新布局     2.5 ms
  2000 条：打开会话    22.6 ms，改变窗口宽度后重新布局    22.4 ms
 20000 条：打开会话   241.5 ms，改变窗口宽度后重新布局   241.9 ms
```

时间和消息的条数成正比，每条约 12 微秒，主要花在 `boundingRect` 排版文字上。2 万条时，打开一个会话要 0.24 秒，更糟的是**用户拖动窗口边缘时，每一帧都要重新计算 2 万个气泡**，窗口会明显地卡。

最初测这组数据时，要是不调用 `doItemsLayout()`，「改变宽度」只要 0.8 毫秒：视图把重新布局推迟到了以后，计时的时候还没有做。不预热的话，200 条反而比 2000 条慢，测到的是加载字体的时间。这两个陷阱都会让测量结果看起来「很快」或者「反常」，测界面的性能时要格外小心。

所以 QtChat 打开会话时**只加载最近的 200 条**：

```cpp
void MainWindow::openChat(const QString &peer)
{
    m_messages.load(peer, m_client->store().recent(peer));     // recent 默认最多 200 条
    m_contacts.clearUnread(peer);
    // ……选中联系人、设置标题
    m_input->setFocus();
    scrollToBottomLater();
}
```

更早的记录还在数据库中，完整的实现应该在用户滚动到顶部时再加载一批（`canFetchMore` / `fetchMore`），这留作练习。

## 5. 交互的细节

### 5.1 回车发送

`QPlainTextEdit` 中按回车是换行。聊天软件的习惯是回车发送、Shift+回车换行。用第三章的事件过滤器，在按键到达输入框之前拦下来：

```cpp
bool MainWindow::eventFilter(QObject *obj, QEvent *e)
{
    if (obj == m_input && e->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(e);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
            && !(key->modifiers() & Qt::ShiftModifier)) {
            sendDraft();
            return true;                                    // 拦截：不让输入框插入换行
        }
    }
    return QMainWindow::eventFilter(obj, e);
}
```

`Key_Return` 是主键盘的回车，`Key_Enter` 是小键盘的回车，两个都要处理。返回 `true` 表示事件已经处理了，输入框收不到它，也就不会插入换行。

### 5.2 新消息来了，要不要滚到底部

```cpp
void MainWindow::onMessageAdded(const ChatMessage &m)
{
    bool current = m.peer == m_messages.peer();
    m_contacts.setLastText(m.peer, m.body, !m.outgoing && !current);   // 不是当前会话的，未读数加 1
    if (!current)
        return;
    QScrollBar *bar = m_messageView->verticalScrollBar();
    bool atBottom = bar->value() >= bar->maximum() - 4;
    m_messages.append(m);
    if (atBottom || m.outgoing)                             // 用户在翻看历史时，不要把他拉到底部
        scrollToBottomLater();
}

void MainWindow::scrollToBottomLater()
{
    // 新的一行要等视图重新布局后才有位置，所以推迟到下一轮事件循环（《跨线程的信号与槽》2.4 节）
    QTimer::singleShot(0, m_messageView, &QListView::scrollToBottom);
}
```

- 用户正停在最底部，或者是自己刚发的消息，就滚到底部；如果用户正在往上翻看历史，来了一条新消息就把他拉回底部，体验很差。判断要在 `append` **之前**做，`append` 之后滚动条的最大值就变了。
- 滚动要推迟到下一轮事件循环：`append` 之后，视图只是记下了「需要重新布局」，新的一行还没有位置，这时 `scrollToBottom()` 滚到的是旧的底部。`QTimer::singleShot(0, ...)` 和第三章说的同一线程中的队列连接一样，等当前的处理结束后再执行。

### 5.3 重连后刷新联系人：不要重置模型

每次登录（包括重连后的登录），服务器都会发来完整的用户列表。最初的写法是重置模型：

```cpp
void ContactModel::reset(const QList<QPair<QString, bool>> &users)
{
    beginResetModel();
    m_contacts.clear();
    for (const auto &[name, online] : users)
        m_contacts.append({name, online, 0, {}});
    endResetModel();
}
```

结果是每次网络波动重连后，**选中的联系人没了，所有的未读数也清零了**：重置模型时，视图会丢掉选中状态和滚动位置（第二章 2.2 篇第 5.3 节），而重建列表时未读数也被清掉了。改成逐个更新：

```cpp
void ContactModel::sync(const QList<QPair<QString, bool>> &users)
{
    // 重连后服务器发来完整的列表。不用重置模型，否则选中的联系人、未读数都会丢失
    for (Contact &c : m_contacts)
        c.online = false;
    if (!m_contacts.isEmpty())
        emit dataChanged(index(0), index(int(m_contacts.size()) - 1), {Qt::DecorationRole, OnlineRole});
    for (const auto &[name, online] : users)
        setOnline(name, online);                // 已有的更新状态，新的插入到末尾
}
```

**重置模型只用在数据整体换掉的时候**，例如切换会话时，`MessageModel::load` 用的就是重置，因为整个列表确实都换了；局部的变化用 `dataChanged` 和插入、删除行。

## 6. 登录与启动

### 6.1 登录对话框

第二章 2.1 篇的登录框加了一个「服务器」输入框，并用 `QSettings` 记住上次的输入：

```cpp
LoginDialog::LoginDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("登录 QtChat");
    QSettings settings;                                   // 记住上次的输入
    m_user = new QLineEdit(settings.value("user").toString());
    m_user->setValidator(new QRegularExpressionValidator(
        QRegularExpression("[A-Za-z][A-Za-z0-9_]{0,15}"), m_user));
    m_server = new QLineEdit(settings.value("server", "127.0.0.1:9000").toString());
    // ……表单、按钮；点「登录」时保存
}
```

`QSettings` 不需要指定文件名，它根据 `main` 中设置的组织名和程序名，把设置保存在各个平台约定的位置：macOS 上是 `~/Library/Preferences/` 下的 plist 文件，Windows 上是注册表，Linux 上是 `~/.config/` 下的 ini 文件。

### 6.2 main

```cpp
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("lucky-notes");
    QApplication::setApplicationName("QtChat");             // 决定数据和设置保存的位置
    // ……加载 Qt 的中文翻译（第 4 篇会修改这里）

    // 可以用命令行参数跳过登录框：QtChat --user lucky --server 127.0.0.1:9000
    QCommandLineParser parser;
    parser.addOption({"user", "用户名", "name"});
    parser.addOption({"server", "服务器地址", "host:port", "127.0.0.1:9000"});
    parser.process(app);

    QString user = parser.value("user");
    QString server = parser.value("server");
    if (user.isEmpty()) {
        LoginDialog login;
        if (login.exec() != QDialog::Accepted)              // 程序刚启动，用 exec 是安全的
            return 0;
        user = login.user();
        server = login.host() + ":" + QString::number(login.port());
    }

    ChatClient client;
    MainWindow window(&client);
    if (!client.start(user, server.section(':', 0, 0), quint16(server.section(':', 1, 1).toUInt()))) {
        QMessageBox::critical(nullptr, "QtChat", "无法打开本地的消息数据库。");
        return 1;
    }
    window.show();
    return app.exec();
}
```

- 登录框用 `exec()`：第二章 2.1 篇说过 `exec()` 的嵌套事件循环有风险，但这里是程序刚启动，还没有任何网络连接和定时器，不会有东西在对话框打开期间被删除。
- `QCommandLineParser` 解析命令行参数，提供 `--user` 时跳过登录框，在同一台电脑上开多个客户端测试时很方便。
- `client` 和 `window` 都在 `main` 的栈上，`window` 后创建、先析构，析构时 `client` 还在（第一章《对象树与内存管理》第 2 节的声明顺序）。

### 6.3 用脚本驱动界面

本章的截图不是手动操作后截的，而是两个客户端按脚本自动操作，然后用 `grab()` 保存的。`CMakeLists.txt` 中有一个开发用的选项：

```cmake
# 开发用：用脚本驱动界面、自动截图（见 devtools.cpp），发布时关掉
option(CHAT_DEVTOOLS "编译脚本驱动的测试工具" OFF)
```

打开后，程序启动时读取环境变量 `CHAT_SCRIPT` 中的脚本，用定时器依次执行（第一章、第二章的实验中模拟点击和截图用的是同样的方法）：

```bash
CHAT_SCRIPT="wait 2500; open zhangsan; wait 500; type 好啊，几点？; send; wait 3500; type 好的，不见不散; send; wait 3000; shot lucky.png; quit" \
    ./QtChat.app/Contents/MacOS/QtChat --user lucky
```

这样一来，同一个场景可以反复、精确地重现，界面改了以后重新跑一遍脚本就能更新所有的截图。下一篇测试「服务器卡住时发出的消息」，也是靠它和 `docker pause` 配合完成的。

## 7. 小结

- 客户端分层：`Connection` 管连接，`ChatClient` 管协议，`MessageStore` 管存储，界面通过信号得知变化。
- 自定义委托重写 `sizeHint`（每一项多大）和 `paint`（怎样画），两者用同一个函数计算布局；委托不保存状态，颜色尽量取自调色板。
- 高度不固定的列表不能用 `uniformItemSizes`，布局代价和条数成正比（实测每条 12 微秒），窗口改变宽度时还要全部重算。只加载最近的一部分，需要时再加载更早的。
- 测量界面性能时要强制完成布局（`doItemsLayout`），并预热，否则结果不可信。
- 回车发送用事件过滤器；新消息只在用户停在底部时才自动滚动，滚动要推迟到布局完成之后。
- 整体换掉数据时才重置模型，局部变化用 `dataChanged` 和插入、删除行，否则会丢失选中状态。

## 练习

1. 实现「滚动到顶部时加载更早的 200 条」：在 `MessageModel` 中实现 `canFetchMore` 和 `fetchMore`，从 `MessageStore` 按时间查询更早的消息，用 `beginInsertRows(QModelIndex(), 0, n - 1)` 插到开头。插入后，怎样保持用户正在看的那条消息不跳动？
2. 在两条消息的时间相差超过 5 分钟时，在它们之间显示一行居中的时间（类似「昨天 21:30」）。应该在委托中处理，还是在模型中插入一种特殊的行？
3. 让气泡中的文字可以选中复制。为什么用委托画出来的文字不能直接选中？`QListView` 的 `setIndexWidget` 或者 `openPersistentEditor` 能解决吗，代价是什么？
4. 在深色模式下运行 QtChat（第二章《样式表与深色模式》中的 `setColorScheme`），截图检查对方气泡的颜色。`AlternateBase` 在深色模式下是什么颜色？
