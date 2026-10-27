---
title: Model/View：显示大量数据
date: 2026-10-28 10:00:00
updated: 2026-10-28 10:00:00
categories:
  - 客户端开发
  - 界面开发
tags:
  - Qt
  - C++
  - Model/View
order: 2
description: 往 QListWidget 里放 100 万个联系人，多占了 247 MB 内存，显示出来要将近 4 秒。换成自己写的模型：数据不存，要哪一行就现场生成哪一行，内存只多 40 MB，可是显示仍然要 3.9 秒。在 data() 中计数，发现视图调用了 800 万次 data()，把 100 万行每一行都问了一遍；打开 setUniformItemSizes 之后，只调用了 176 次，涉及 25 行，显示只要 100 毫秒。没有它的时候更糟：修改任何一行，哪怕是看不见的一行，又是 800 万次调用、4 秒卡顿。最后实测代理模型：在 100 万行中过滤只要 150～220 毫秒，排序却用了 31 秒，而直接对数据 std::sort 只要 68 毫秒。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解 Model/View 把数据和显示分开的思路；会继承 `QAbstractListModel`、`QAbstractTableModel` 写自己的模型，实现 `rowCount`、`data`、`headerData`，支持编辑（`flags`、`setData`）；理解角色（role）；理解视图只请求可见行的数据，知道 `setUniformItemSizes` 为什么对大列表至关重要；修改数据时会正确地通知视图；会用 `QSortFilterProxyModel` 过滤和排序，并知道它的代价。
>
> **前置知识**：本章上一篇；第一章的信号与槽。了解 `std::sort` 的复杂度（数据结构专栏「排序与查找」一章）。
>
> **实验环境**：macOS 27.0（Apple M6），Qt 6.12.0，Release 构建。计时实验用 `offscreen` 平台插件运行（第一章《环境与第一个 Qt 程序》第 5 节），结果比在屏幕上显示更稳定；内存是进程常驻内存（RSS）的增加量。

## 1. QListWidget 的问题

聊天客户端要显示联系人列表。上一章用过的 `QListWidget` 用起来最简单：

```cpp
QListWidget list;
for (int i = 0; i < N; ++i)
    list.addItem(QString("联系人 %1").arg(i));      // 每一行是一个 QListWidgetItem
list.show();
```

几十个联系人时没有任何问题。可是如果要显示的是聊天记录、日志、文件列表，几十万行很常见。分别放 1 万、10 万、100 万行，测量填充数据的时间、窗口显示出来的时间和增加的内存：

```
QListWidget     10000 行: 填充     7.4 ms，显示   145.7 ms，内存 +  30.2 MB
QListWidget    100000 行: 填充    20.8 ms，显示   433.9 ms，内存 +  49.3 MB
QListWidget   1000000 行: 填充   207.5 ms，显示  3951.0 ms，内存 + 247.0 MB
```

100 万行时，窗口要将近 4 秒才显示出来，这期间程序完全没有反应；内存多了 247 MB，平均每行 250 字节左右。这些内存的大部分是 `QListWidgetItem` 对象本身：每一行都是一个独立的对象，保存着文字、图标、颜色、字体、对齐方式等一整套属性。可是屏幕上一次只能显示 20 多行。

## 2. Model/View 的思路

Qt 的 **Model/View** 把「数据」和「显示」分成两部分：

- **模型**（model）：回答关于数据的问题：一共有几行？第 i 行要显示什么文字？用什么颜色？
- **视图**（view）：负责显示，需要画哪一行，就去问模型要那一行的数据。

```
       rowCount()?          ┌──────────┐
  ┌──────────────────────── │          │
  │    data(第 i 行)?        │   视图    │ ── 画在屏幕上
  ▼  ◄───────────────────── │ QListView│
┌────────┐                  └──────────┘
│  模型   │  dataChanged、rowsInserted ……
│        │ ─────────────────────────────►   （数据变了，通知视图）
└────────┘
```

其实 `QListWidget` 内部也是 Model/View：它自己带了一个模型，模型中存着所有的 `QListWidgetItem`。用 Model/View 的好处是**模型可以自己决定数据怎样存**：可以存在数组里，可以存在数据库里，也可以根本不存，需要时现场算出来。

同一个模型还可以同时显示在多个视图中（例如一个列表和一个表格），一处修改，所有视图一起更新。

Qt 提供的视图有 `QListView`（列表）、`QTableView`（表格）、`QTreeView`（树），模型的基类有 `QAbstractListModel`、`QAbstractTableModel`、`QAbstractItemModel`（树）。

## 3. 写一个列表模型

### 3.1 最少需要两个函数

继承 `QAbstractListModel`，至少实现 `rowCount` 和 `data`：

```cpp
#include <QAbstractListModel>

// 数据不保存在模型中，而是需要哪一行，就现场算出哪一行
class ContactModel : public QAbstractListModel
{
public:
    explicit ContactModel(int n) : m_n(n) {}
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : m_n;
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (role == Qt::DisplayRole)
            return QString("联系人 %1").arg(index.row());
        return {};
    }

private:
    int m_n;
};
```

使用时，把模型交给视图：

```cpp
ContactModel model(N);
QListView view;
view.setModel(&model);
view.show();
```

几个新概念：

- **`QModelIndex`**：表示模型中的一个位置：第几行、第几列、父节点是谁（树形模型才用到）。`index.row()` 就是行号。
- **`parent.isValid() ? 0 : m_n`**：在树形结构中，`rowCount(parent)` 问的是「`parent` 下面有几个子节点」。列表只有一层，只有顶层（`parent` 无效）有 `m_n` 行，任何一行下面都没有子行。这是 Qt 文档要求的写法：对有效的 `parent` 必须返回 0，否则视图会以为每一行下面还有子行。
- **`QVariant`**：可以装任何类型的值（字符串、数字、颜色……）的容器。不同的角色返回不同类型的数据，所以返回值用 `QVariant`；返回一个空的 `QVariant`（`{}`）表示「没有这项数据，用默认值」。
- **角色**（role）：一行数据不只有文字，还有图标、颜色、字体、提示……视图用 `role` 说明它要哪一种，第 4.2 节会看到视图到底问了哪些角色。

### 3.2 内存小了，可是还是慢

用这个模型重复第 1 节的实验：

```
QListView             10000 行: 填充     0.5 ms，显示   133.9 ms，内存 +  28.1 MB
QListView            100000 行: 填充     0.4 ms，显示   440.8 ms，内存 +  28.5 MB
QListView           1000000 行: 填充     0.3 ms，显示  3921.7 ms，内存 +  40.2 MB
```

「填充」几乎不花时间，因为什么都没有存。100 万行的内存从 247 MB 降到了 40 MB，其中大约 28 MB 是显示任何一个窗口都要的基础开销（1 万行时也是这么多）。

可是**显示时间没有变**，100 万行还是要 3.9 秒。视图在这 3.9 秒里做了什么？

## 4. 视图到底问了模型什么

### 4.1 给 data() 计数

在 `data()` 中记下每一次调用的角色和行号：

```cpp
QVariant data(const QModelIndex &index, int role) const override
{
    ++calls[role];                  // mutable QMap<int, long> calls;
    rows.insert(index.row());       // mutable std::set<int> rows;
    if (role == Qt::DisplayRole)
        return QString("联系人 %1").arg(index.row());
    return {};
}
```

`data()` 是 `const` 成员函数，所以计数用的两个成员要声明成 `mutable`。100 万行的列表，依次做四件事：显示窗口、滚动到第 50 万行、修改一行可见的、修改一行看不见的（第 10 行），每一步之后统计：

```
显示                        4357.7 ms，data() 调用 8000392 次，涉及 1000000 行，DisplayRole 1000052 次
滚动到中间                    7.1 ms，data() 调用     393 次，涉及      24 行，DisplayRole 52 次
修改一行（可见）            4191.3 ms，data() 调用 8000016 次，涉及 1000000 行，DisplayRole 1000002 次
修改一行（不可见）          4299.0 ms，data() 调用 8000368 次，涉及 1000000 行，DisplayRole 1000049 次
```

显示时，`data()` 被调用了 **800 万次**，100 万行**每一行都被问到了**，每行 8 次。原因是 `QListView` 默认认为每一行的高度都可能不一样（文字可能换行、字体可能不同），为了算出滚动条有多长、第 50 万行在什么位置，它必须知道**每一行**的大小，于是把每一行的文字、字体、图标都问了一遍。

更糟的是后两行：**修改任何一行**，视图都认为行高可能变了，重新计算了全部 100 万行的大小，又是 4 秒的卡顿，即使被修改的那一行根本不在屏幕上。

### 4.2 setUniformItemSizes

聊天列表、联系人列表的每一行高度都一样。告诉视图这一点：

```cpp
view.setUniformItemSizes(true);
```

视图只要量出一行的大小，乘以行数就是总高度，第 i 行的位置就是 i 乘以行高。再测一次：

```
显示                          99.6 ms，data() 调用     176 次，涉及      25 行，DisplayRole 25 次
滚动到中间                 < 5 ms，data() 调用     169 次，涉及      24 行，DisplayRole 24 次
修改一行（可见）            < 5 ms，data() 调用     176 次，涉及      25 行，DisplayRole 25 次
修改一行（不可见）          < 5 ms，data() 调用     176 次，涉及      25 行，DisplayRole 25 次
```

`data()` 从 800 万次降到了 176 次，只涉及屏幕上能看到的 24 行（加上用来量行高的 1 行）。显示时间从 3.9 秒降到 100 毫秒，其中大部分是创建窗口本身的时间；不同行数的实验中，它都在 90～100 毫秒：

```
QListView(uniform)    10000 行: 填充     0.2 ms，显示    88.2 ms，内存 +  28.2 MB
QListView(uniform)   100000 行: 填充     0.2 ms，显示    92.4 ms，内存 +  28.6 MB
QListView(uniform)  1000000 行: 填充     0.3 ms，显示   102.3 ms，内存 +  40.1 MB
```

**显示时间不再随行数增长**。这才是 Model/View 真正的好处：视图只为看得见的行工作，有多少行都无所谓。

修改一行时，视图重画了整个可见区域（24 行，用事件过滤器可以看到视口收到了一个 280×398 的 `Paint` 事件），即使改的是看不见的第 10 行。24 行的重画只要几毫秒，可以忽略。

> 每一行的高度确实不一样时（例如聊天气泡的高度取决于消息的长度），就不能打开 `uniformItemSizes`。这时要避免一次性显示几十万行：只加载最近的几千条，滚动到顶部时再加载更早的（`canFetchMore` / `fetchMore`），或者自己缓存每一行的高度。第 5 章的聊天窗口会遇到这个问题。

把打印每种角色次数的代码打开，看看视图对每一行要了哪些数据：

```
    DisplayRole            25      显示的文字
    DecorationRole         25      图标
    FontRole               25      字体
    TextAlignmentRole      25      对齐方式
    BackgroundRole         25      背景色
    ForegroundRole         25      文字颜色
    CheckStateRole         25      复选框的状态
    SizeHintRole           1       建议大小（只问了一行）
```

画一行，视图要问 7 次。我们的模型只回答了 `DisplayRole`，其他都返回空的 `QVariant`，视图就用默认值。下一节的表格模型会回答其中几种。

所以 **`data()` 必须快**：它会被频繁调用，每次滚动、每次重画都会调用。不要在 `data()` 中读文件、查数据库、做网络请求，这些都要事先做好，把结果存在模型中。

## 5. 表格模型、角色与编辑

### 5.1 一个联系人表格

联系人有姓名、电话、在线状态，用表格显示，继承 `QAbstractTableModel`，多实现一个 `columnCount`：

```cpp
#include <QAbstractTableModel>
#include <QColor>
#include <vector>

struct Contact {
    QString name;
    QString phone;
    bool online;
};

class ContactTable : public QAbstractTableModel
{
public:
    int rowCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : int(m_rows.size());
    }
    int columnCount(const QModelIndex &parent = {}) const override
    {
        return parent.isValid() ? 0 : 3;
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        const Contact &c = m_rows[index.row()];
        switch (role) {
        case Qt::DisplayRole:
        case Qt::EditRole:
            if (index.column() == 0) return c.name;
            if (index.column() == 1) return c.phone;
            return c.online ? "在线" : "离线";
        case Qt::ForegroundRole:                    // 文字颜色
            if (index.column() == 2)
                return QColor(c.online ? "#2e9e44" : "#999999");
            break;
        case Qt::TextAlignmentRole:
            if (index.column() == 2)
                return int(Qt::AlignCenter);
            break;
        }
        return {};
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        if (role == Qt::DisplayRole && orientation == Qt::Horizontal)
            return QStringList{"姓名", "电话", "状态"}[section];
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    // flags 和 setData 见 5.2

private:
    std::vector<Contact> m_rows{{"张三", "138 0000 0001", true},
                                {"李四", "139 0000 0002", false},
                                {"王五", "137 0000 0003", true},
                                {"赵六", "136 0000 0004", false}};
};
```

```cpp
ContactTable model;
QTableView view;
view.setModel(&model);
view.horizontalHeader()->setStretchLastSection(true);   // 最后一列占满剩余宽度
view.resizeColumnsToContents();
```

<img src="/img/client/table-view.png" alt="QTableView 显示的联系人表格，状态列用颜色区分" width="360">

- 「状态」列回答了 `ForegroundRole`（绿色、灰色）和 `TextAlignmentRole`（居中），颜色和对齐是**模型**决定的，视图只负责照着画。
- `headerData` 提供表头：横向是列名；纵向没有处理，交给基类，默认显示行号 1、2、3、4。

### 5.2 支持编辑

让「姓名」一列可以双击修改，要再实现两个函数：

```cpp
Qt::ItemFlags flags(const QModelIndex &index) const override
{
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (index.column() == 0)
        f |= Qt::ItemIsEditable;                // 只有姓名可以编辑
    return f;
}
bool setData(const QModelIndex &index, const QVariant &value, int role) override
{
    if (role != Qt::EditRole || index.column() != 0 || value.toString().isEmpty())
        return false;                           // 不接受空的姓名
    m_rows[index.row()].name = value.toString();
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    return true;
}
```

- `flags` 告诉视图哪些格子可以编辑、选择、勾选、拖动。
- 用户编辑完成后，视图调用 `setData`。模型自己决定接不接受：返回 `false`，编辑就被撤销了。
- `data()` 中 `EditRole` 和 `DisplayRole` 返回同样的值：`EditRole` 是开始编辑时编辑框中的初始内容，两者可以不同，例如显示「1.5 MB」，编辑时是「1572864」。

### 5.3 修改数据时要通知视图

5.2 节的 `setData` 最后发出了 `dataChanged` 信号。**模型中的数据变了，必须发出对应的信号，视图才会更新**，否则视图显示的还是旧的数据。不同的修改用不同的通知：

| 修改 | 通知方式 |
|---|---|
| 某几格的内容变了 | `emit dataChanged(左上角, 右下角, 角色列表)` |
| 插入行 | 修改前 `beginInsertRows(parent, 第一行, 最后一行)`，修改后 `endInsertRows()` |
| 删除行 | `beginRemoveRows(...)` / `endRemoveRows()` |
| 整体都变了（例如重新加载） | `beginResetModel()` / `endResetModel()` |

比如来了一个新的联系人，加到末尾：

```cpp
void ContactTable::append(const Contact &c)
{
    int row = int(m_rows.size());
    beginInsertRows({}, row, row);      // 先告诉视图：要在第 row 行插入 1 行
    m_rows.push_back(c);
    endInsertRows();                    // 插好了
}
```

插入和删除要用一对 `begin` / `end` 包起来，是因为视图需要在数据变化**之前**做准备：记住当前选中的行、滚动的位置，变化之后再把它们调整到正确的地方。如果只在变化之后发通知，视图就不知道「原来的第 5 行」现在跑到了哪里。

最常见的错误是忘了发通知，或者用 `beginResetModel` 处理所有的修改：重置会让视图丢掉选中状态和滚动位置，界面会跳回顶部。

## 6. 过滤和排序：QSortFilterProxyModel

联系人列表通常有一个搜索框。过滤和排序不用改模型本身，在模型和视图之间插入一个**代理模型** `QSortFilterProxyModel`：

```
ContactModel（100 万行）──► QSortFilterProxyModel（只露出匹配的行）──► QListView
```

```cpp
ContactModel model(1000000);
QSortFilterProxyModel proxy;
proxy.setSourceModel(&model);
QListView view;
view.setUniformItemSizes(true);
view.setModel(&proxy);                      // 视图看到的是代理模型

proxy.setFilterFixedString(key);            // 只保留包含 key 的行
```

把搜索框的 `textChanged` 信号连接到 `setFilterFixedString`，就是一个边输入边过滤的搜索。测量 100 万行时的速度：

```
过滤 "12345":   160.9 ms，剩下 20 行
过滤 "999":     148.0 ms，剩下 3700 行
过滤 "7":       220.8 ms，剩下 468559 行
取消过滤并按文字倒序排序: 31355.9 ms，第一行是 联系人 999999
```

过滤要把每一行都检查一遍，100 万次 `data()`，150～220 毫秒。用户每输入一个字就卡顿 0.2 秒，勉强可以接受；行数再多，就要等用户停止输入一会儿（比如 300 毫秒）再过滤，或者把过滤放到后台线程（第 3 章）。

排序用了 **31 秒**。作为对比，把 100 万个同样的字符串放进 `QStringList`，直接用 `std::sort` 排序：

```cpp
std::sort(names.begin(), names.end(), std::greater<QString>());
```

```
生成 100 万个字符串:   88.2 ms
std::sort 倒序:        67.5 ms，第一个是 联系人 999999
```

只要 68 毫秒，相差 460 倍。排序 100 万个元素大约要比较 2000 万次（n log n），代理模型每比较一次，都要对两行各调用一次 `data()`：我们的模型每次都要现场生成一个字符串，再包装成 `QVariant`，然后比较两个 `QVariant`。4000 万次 `data()`，平均每次不到 1 微秒，加起来就是 30 秒。`std::sort` 比较的是已经存好的字符串，没有这些额外的开销。

所以：

- 数据量不大（几千、几万行），`QSortFilterProxyModel` 很方便，直接用；
- 数据量很大，或者 `data()` 不是简单地返回一个存好的值，**排序应该在模型内部对原始数据做**，排好后发出 `layoutChanged`（或者干脆重置模型）。数据来自数据库时，直接用 `ORDER BY` 让数据库排序（数据库专栏第 3 章）。

## 7. 小结

- `QListWidget` 为每一行创建一个对象，几十万行时内存和速度都不可接受。Model/View 把数据（模型）和显示（视图）分开，视图需要哪一行，就向模型要哪一行。
- 写列表模型至少实现 `rowCount` 和 `data`；表格模型再加 `columnCount`、`headerData`；可编辑的模型再加 `flags`、`setData`。
- `data()` 按角色返回数据：文字、颜色、对齐、图标……返回空的 `QVariant` 表示用默认值。`data()` 会被频繁调用，必须快。
- 每一行一样高时一定要 `setUniformItemSizes(true)`：100 万行的列表，`data()` 调用从 800 万次降到 176 次，显示从 3.9 秒降到 0.1 秒，修改一行也不再卡顿。
- 修改数据要通知视图：`dataChanged`，插入删除用 `begin…` / `end…` 包起来，尽量不用重置。
- `QSortFilterProxyModel` 过滤和排序很方便，但大数据量的排序代价很高，应该在模型内部或者数据库中完成。

视图的样子到现在都是系统默认的。下一篇用样式表改变控件的外观，并让程序跟随系统的深色模式。

## 练习

1. 给第 3 节的 `ContactModel` 增加 `DecorationRole`：偶数行返回一个红色的小圆点图标（`QPixmap` 填充颜色即可），奇数行返回空。
2. 第 4.1 节中，打开 `uniformItemSizes` 前后，「滚动到中间」都只调用了几百次 `data()`。为什么不打开它时，滚动并不慢？
3. 给第 5 节的表格模型加一个 `remove(int row)` 函数，用 `beginRemoveRows` / `endRemoveRows` 删除一行。如果忘了调用 `beginRemoveRows`，直接删掉数据再 `endRemoveRows`，会发生什么？用 ASan 运行试试。
4. 重写 `QSortFilterProxyModel::lessThan`，按联系人编号（数字）而不是文字排序，使「联系人 9」排在「联系人 10」前面。排序 100 万行要多久？怎样才能更快？
