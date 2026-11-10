---
title: 组合模式
date: 2026-11-10 16:00:00
updated: 2026-11-10 16:00:00
categories:
  - 设计模式
  - 结构型模式
tags:
  - 设计模式
  - 组合
  - 递归
  - 深拷贝
order: 3
description: 文件和文件夹分开处理，每个函数都要判断节点的种类、自己递归；加一种「压缩包」时漏改一处，项目大小从 1116 KB 算成了 10332 KB。组合模式让叶子和容器实现同一个接口，容器把操作递归地交给子节点。实测：用 std::filesystem 把博客的 source 目录读成组合结构，856 个文件共 115143463 字节，和 find、GNU du 逐字节一致；MP4 的 44 个 box 也是一棵组合树，每个容器的长度都等于子 box 之和。再讨论透明与安全两种接口、整棵树的深拷贝（压缩包忘了重写 clone()，两个编译器都不报警告），以及递归太深的问题：Debug 构建下销毁一条 1.5 万层的链就会栈溢出，改成显式栈后 1000 万层也没问题。
top_group_list: true
disableNunjucks: true
viz: [dp-composite]
---

> **本文目标**：理解组合模式解决的问题；会用 `std::unique_ptr` 写出叶子和容器统一接口的树；分清「透明」和「安全」两种接口设计；会深拷贝一棵树；知道递归遍历和递归析构在树很深时的风险，会改成显式栈。
>
> **前置知识**：上一章的《原型模式》（`clone()`）；第 1 章的《组合优于继承与 C++ 的四种多态》；C/C++ 专栏的《智能指针：unique_ptr、shared_ptr 与 weak_ptr》《函数》（调用栈与栈溢出）。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`。完整代码在仓库的 `projects/dp-lab/13-composite/` 目录。

## 1. 问题：文件和文件夹，处处要分开处理

写一个简单的文件管理器：显示目录树、统计文件个数和总大小。最直接的写法是用一个结构体表示所有节点，加一个字段区分它是文件还是文件夹。后来又加了一种节点：压缩包（`.zip`）。它像文件夹一样可以打开、看到里面的文件，但它占的空间是压缩后的大小：

```cpp
// 《组合》：叶子和容器分开处理——每个函数都要判断节点是文件还是文件夹，加了压缩包之后算错了大小
#include <iostream>
#include <memory>
#include <string>
#include <vector>

enum class Kind { File, Folder, Archive };   // Archive（压缩包）是后来加的

struct Node {
    Kind kind;
    std::string name;
    long long bytes = 0;                          // 文件、压缩包自己的大小；文件夹不用
    std::vector<std::unique_ptr<Node>> children;  // 文件夹、压缩包里的内容；文件不用
};

Node* add(Node* parent, Kind kind, std::string name, long long bytes = 0) {
    parent->children.push_back(std::make_unique<Node>(Node{kind, std::move(name), bytes, {}}));
    return parent->children.back().get();
}

// 每个函数都要自己判断节点的种类、自己递归
long long totalSize(const Node* n) {
    if (n->kind == Kind::File) return n->bytes;
    long long sum = 0;                            // 加 Archive 时漏改了这里：压缩包走进了「文件夹」的分支
    for (const auto& c : n->children) sum += totalSize(c.get());
    return sum;
}

int countFiles(const Node* n) {
    if (n->kind == Kind::File || n->kind == Kind::Archive) return 1;   // 这里记得改了：压缩包算一个文件
    int count = 0;
    for (const auto& c : n->children) count += countFiles(c.get());
    return count;
}

void print(const Node* n, int depth = 0) {
    std::cout << std::string(depth * 2, ' ') << n->name;
    if (n->kind == Kind::Folder) std::cout << "/";
    else if (n->kind == Kind::Archive) std::cout << "（压缩包，" << n->bytes / 1024 << " KB）";   // 这里也改了
    else std::cout << "  " << n->bytes / 1024 << " KB";
    std::cout << '\n';
    for (const auto& c : n->children) print(c.get(), depth + 1);
}

int main() {
    Node root{Kind::Folder, "project", 0, {}};
    add(&root, Kind::File, "README.md", 2048);
    Node* src = add(&root, Kind::Folder, "src");
    add(src, Kind::File, "main.cpp", 12288);
    add(src, Kind::File, "player.cpp", 30720);
    Node* assets = add(&root, Kind::Folder, "assets");
    add(assets, Kind::File, "logo.png", 49152);
    Node* zip = add(assets, Kind::Archive, "logs.zip", 1048576);         // 压缩后 1 MB
    add(zip, Kind::File, "decode.log", 6291456);                          // 解压后 6 MB + 4 MB
    add(zip, Kind::File, "encode.log", 4194304);

    print(&root);
    std::cout << countFiles(&root) << " 个文件，共 " << totalSize(&root) / 1024 << " KB\n";
}
```

```text
project/
  README.md  2 KB
  src/
    main.cpp  12 KB
    player.cpp  30 KB
  assets/
    logo.png  48 KB
    logs.zip（压缩包，1024 KB）
      decode.log  6144 KB
      encode.log  4096 KB
5 个文件，共 10332 KB
```

一个 1 MB 的压缩包，加上几十 KB 的文件，总大小却显示 10332 KB。原因在 `totalSize`：它原来写的是「是文件就返回大小，否则把子节点加起来」，加压缩包的人改了 `countFiles` 和 `print`，漏了这里，压缩包走进了文件夹的分支，算成了解压后的 10 MB。

这和第 1 章《为什么需要设计模式》里的散弹式修改是同一类问题，但这里还有树形结构特有的麻烦：

- **每个操作都要判断节点的种类**：`totalSize`、`countFiles`、`print` 开头都是 `if (kind == ...)`。以后再加「搜索」「删除」「复制」，每个函数都要再写一遍判断。
- **每个操作都要自己递归**：遍历子节点、累加结果的代码在每个函数里重复一遍。
- **客户代码必须知道手里的是文件还是文件夹**：选中的是一个文件、一个文件夹还是一个压缩包，调用方要分别处理。
- **一个结构体装下了所有种类的字段**：文件用不到 `children`，文件夹用不到 `bytes`，谁该用哪个字段只能靠约定。

## 2. 组合模式

### 2.1 意图

**组合模式**（Composite）：将对象组合成树形结构以表示「部分-整体」的层次结构，使得用户对单个对象和组合对象的使用具有一致性。

关键在后半句：**单个对象**（叶子，比如文件）和**组合对象**（容器，比如文件夹）实现同一个接口，调用者不需要区分它们。容器实现接口的方式，是把请求转发给自己的子节点，再把结果汇总起来。子节点可能又是容器，于是请求沿着树递归下去：

```cpp
// 《组合》：文件和文件夹实现同一个接口，文件夹递归地交给子节点处理；加压缩包只需要加一个类
#include <iostream>
#include <memory>
#include <string>
#include <vector>

class Component {
public:
    explicit Component(std::string name) : name_(std::move(name)) {}
    virtual ~Component() = default;
    const std::string& name() const { return name_; }
    virtual long long size() const = 0;
    virtual int countFiles() const = 0;
    void print() const { printAt(0); }
    virtual void printAt(int depth) const = 0;   // 缩进 depth 层打印自己（和子节点）
protected:
    void indent(int depth) const { std::cout << std::string(depth * 2, ' ') << name_; }
private:
    std::string name_;
};

class File : public Component {                   // 叶子
public:
    File(std::string name, long long bytes) : Component(std::move(name)), bytes_(bytes) {}
    long long size() const override { return bytes_; }
    int countFiles() const override { return 1; }
    void printAt(int depth) const override { indent(depth); std::cout << "  " << bytes_ / 1024 << " KB\n"; }
private:
    long long bytes_;
};

class Folder : public Component {                 // 容器：持有子节点，把操作交给它们
public:
    using Component::Component;
    template <class T, class... Args>
    T& add(Args&&... args) {                      // 创建一个子节点，返回它的引用，方便继续往里加
        children_.push_back(std::make_unique<T>(std::forward<Args>(args)...));
        return static_cast<T&>(*children_.back());
    }
    long long size() const override {
        long long sum = 0;
        for (const auto& c : children_) sum += c->size();   // 不管子节点是文件还是文件夹
        return sum;
    }
    int countFiles() const override {
        int n = 0;
        for (const auto& c : children_) n += c->countFiles();
        return n;
    }
    void printAt(int depth) const override {
        indent(depth);
        std::cout << "/\n";
        printChildren(depth);
    }
protected:
    void printChildren(int depth) const { for (const auto& c : children_) c->printAt(depth + 1); }
private:
    std::vector<std::unique_ptr<Component>> children_;
};

class Archive : public Folder {                   // 新加的：能像文件夹一样打开，大小却是它自己的
public:
    Archive(std::string name, long long bytes) : Folder(std::move(name)), bytes_(bytes) {}
    long long size() const override { return bytes_; }
    int countFiles() const override { return 1; }
    void printAt(int depth) const override {
        indent(depth);
        std::cout << "（压缩包，" << bytes_ / 1024 << " KB）\n";
        printChildren(depth);
    }
private:
    long long bytes_;
};

int main() {
    Folder root("project");
    root.add<File>("README.md", 2048);
    auto& src = root.add<Folder>("src");
    src.add<File>("main.cpp", 12288);
    src.add<File>("player.cpp", 30720);
    auto& assets = root.add<Folder>("assets");
    assets.add<File>("logo.png", 49152);
    auto& zip = assets.add<Archive>("logs.zip", 1048576);
    zip.add<File>("decode.log", 6291456);
    zip.add<File>("encode.log", 4194304);

    root.print();
    std::cout << root.countFiles() << " 个文件，共 " << root.size() / 1024 << " KB\n";

    // 客户代码不需要区分叶子和容器：选中的可以是文件，也可以是整个文件夹
    const Component* selected[] = {&src, &zip, &root};
    for (const Component* c : selected) std::cout << c->name() << "：" << c->size() / 1024 << " KB\n";
}
```

```text
project/
  README.md  2 KB
  src/
    main.cpp  12 KB
    player.cpp  30 KB
  assets/
    logo.png  48 KB
    logs.zip（压缩包，1024 KB）
      decode.log  6144 KB
      encode.log  4096 KB
5 个文件，共 1116 KB
src：42 KB
logs.zip：1024 KB
project：1116 KB
```

```uml
label: 组合模式：File 和 Folder 都继承 Component；Folder 用 vector<unique_ptr<Component>> 持有任意多个子节点（可以是文件，也可以是文件夹）；Archive 继承 Folder，重写了 size()
classes:
  - { id: C, name: 客户代码, x: 20, y: 40, w: 100 }
  - { id: P, name: Component, stereo: abstract, x: 200, y: 16, w: 250, attrs: ['- name_ : string'], ops: ['*+ size() : long long', '*+ countFiles() : int', '*+ printAt(depth)'] }
  - { id: F, name: File, x: 100, y: 230, w: 200, attrs: ['- bytes_ : long long'], ops: ['+ size()  // 返回 bytes_'] }
  - { id: D, name: Folder, x: 360, y: 230, w: 340, attrs: ['- children_ : vector<unique_ptr<Component>>'], ops: ['+ add<T>(args...) : T&', '+ size()  // 子节点的 size() 之和'] }
  - { id: A, name: Archive, x: 360, y: 400, w: 340, attrs: ['- bytes_ : long long'], ops: ['+ size()  // 返回压缩后的 bytes_'] }
rels:
  - { from: C, to: P, type: depend }
  - { from: F, to: P, type: inherit, fdx: 0, tdx: -60, mid: 190 }
  - { from: D, to: P, type: inherit, fdx: -100, tdx: 40, mid: 190 }
  - { from: A, to: D, type: inherit }
  - { from: D, to: P, type: compose, fm: '', tm: '0..*', label: 'children_', points: [[700, 262], [750, 262], [750, 60], [450, 60]] }
```

和第 1 节对比：

- **客户代码只调用 `size()`**：`main` 最后选中的可以是文件夹 `src`、压缩包 `logs.zip`，也可以是整个项目，代码完全一样。
- **判断种类的 `if` 消失了**：每个类只管自己。`File` 返回自己的大小，`Folder` 把子节点的 `size()` 加起来，至于子节点是什么，交给虚函数去分派。
- **加压缩包只需要加一个类**：`Archive` 继承 `Folder`（复用管理子节点的代码），重写 `size()` 返回自己的大小。`Folder::size()`、`main` 都不用改，也就不会漏改。这就是开闭原则。
- **`unique_ptr` 表达了所有权**：每个节点只属于它的父节点，父节点销毁时子节点自动销毁。`add<T>(...)` 直接在容器里创建子节点并返回它的引用，方便接着往里加。

写这段代码时我踩了一个小坑：一开始把打印声明成 `virtual void print(int depth = 0) const`，`root.print()` 编译不过。`Folder::print` 重写时没有写默认参数，而 `root` 的静态类型是 `Folder`，看不到基类的默认参数。**虚函数的默认参数按静态类型决定**，和实际调用哪个函数无关，所以一般不要给虚函数写默认参数。上面的写法是一个非虚的 `print()` 调用虚的 `printAt(depth)`。

### 2.2 递归是怎样展开的

下面的演示一步步执行 `project.size()`。注意右边的调用栈：每进入一层文件夹，栈上就多一帧；压缩包虽然也有子节点，但它的 `size()` 不往下走。

<div class="dsviz" data-viz="dp-composite"></div>

调用栈最深的时候，等于树的深度（这棵树是 3 层）。第 6 节会看到，树非常深的时候这就成了问题。

### 2.3 三个「组合」

中文里「组合」这个词在设计模式里有三种意思，容易混：

| 说法 | 英文 | 指的是 |
| --- | --- | --- |
| 组合优于继承（第 1 章） | composition | 一个对象**持有**另一个对象来复用它的功能，和继承相对 |
| UML 的组合关系（实心菱形） | composition | 整体和部分**同生共死**的拥有关系，比如 `unique_ptr` 成员 |
| **组合模式**（本文） | Composite | 叶子和容器实现**同一个接口**、容器里可以再放容器的**树形结构** |

组合模式用到了前两种：`Folder` 通过持有子节点来实现功能（对象组合），而且它和子节点之间正好是 UML 的组合关系（类图里的实心菱形）。但让它成为一个「模式」的，是第三点：**容器自己也是一个 `Component`**，所以才能一层套一层，调用者也才能一视同仁。

## 3. 实测：把真实的目录读成一棵树

文件系统就是一棵现成的组合树。用 C++17 的 `std::filesystem` 遍历一个目录，把它读成上面的结构，再和系统工具对照：

<!-- run: off -->
```cpp
// 《组合》：用 std::filesystem 把一个真实的目录读成组合结构，统计大小，和 du 对照
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

class Component {
public:
    explicit Component(std::string name) : name_(std::move(name)) {}
    virtual ~Component() = default;
    const std::string& name() const { return name_; }
    virtual long long size() const = 0;     // 字节数（文件的实际长度，不是占用的磁盘块）
    virtual int files() const = 0;
    virtual int folders() const = 0;
private:
    std::string name_;
};

class File : public Component {
public:
    File(std::string name, long long bytes) : Component(std::move(name)), bytes_(bytes) {}
    long long size() const override { return bytes_; }
    int files() const override { return 1; }
    int folders() const override { return 0; }
private:
    long long bytes_;
};

class Folder : public Component {
public:
    using Component::Component;
    void add(std::unique_ptr<Component> c) { children_.push_back(std::move(c)); }
    long long size() const override {
        long long sum = 0;
        for (const auto& c : children_) sum += c->size();
        return sum;
    }
    int files() const override {
        int n = 0;
        for (const auto& c : children_) n += c->files();
        return n;
    }
    int folders() const override {
        int n = 1;                                      // 自己
        for (const auto& c : children_) n += c->folders();
        return n;
    }
    const std::vector<std::unique_ptr<Component>>& children() const { return children_; }
private:
    std::vector<std::unique_ptr<Component>> children_;
};

// 读取目录：创建树的时候就知道每一项是文件还是目录，所以 add() 只放在 Folder 里（安全的设计）也很好用
std::unique_ptr<Folder> load(const fs::path& dir) {
    auto folder = std::make_unique<Folder>(dir.filename().string());
    for (const auto& e : fs::directory_iterator(dir)) {
        if (e.is_symlink()) continue;                   // 不跟随符号链接（du 也不跟随）
        if (e.is_directory()) folder->add(load(e.path()));
        else if (e.is_regular_file()) folder->add(std::make_unique<File>(e.path().filename().string(), e.file_size()));
    }
    return folder;
}

int main(int argc, char** argv) {
    fs::path dir = argc > 1 ? argv[1] : ".";
    auto root = load(dir);
    std::printf("%s：%d 个文件，%d 个目录，共 %lld 字节（%lld KB）\n", dir.string().c_str(), root->files(),
                root->folders(), root->size(), root->size() / 1024);

    // 第一层按大小排序：客户代码对文件和目录一视同仁，都只调用 size()
    std::vector<const Component*> top;
    for (const auto& c : root->children()) top.push_back(c.get());
    std::sort(top.begin(), top.end(), [](auto* a, auto* b) { return a->size() > b->size(); });
    for (size_t i = 0; i < top.size() && i < 6; ++i)
        std::printf("  %-14s %9lld KB  %5d 个文件\n", top[i]->name().c_str(), top[i]->size() / 1024, top[i]->files());
}
```

读这个博客仓库的 `source/` 目录（写这篇文章之前的版本）：

```text
source：856 个文件，34 个目录，共 115143463 字节（112444 KB）
  music              96893 KB      6 个文件
  _posts              6131 KB    274 个文件
  img                 3313 KB     75 个文件
  media               2924 KB      1 个文件
  _problems           2339 KB    407 个文件
  js                   565 KB     69 个文件
```

用别的方法统计同一个目录：

| 方法 | 结果 |
| --- | --- |
| 本文的组合树（macOS / Clang、Ubuntu / GCC 都一样） | 856 个文件，34 个目录，**115143463 字节** |
| `find source -type f` 逐个取文件长度再相加（macOS `stat -f %z`、Linux `-printf '%s\n'`） | 856 个文件，**115143463 字节** |
| GNU `du -sb source`（Ubuntu 24.04，`-b` 表示按字节统计实际长度） | **115143463 字节** |
| macOS `du -A -sk source`（`-A` 表示实际长度） | 112698 KB |
| macOS `du -sk source`（占用的磁盘空间） | 114640 KB |

前三种逐字节一致。后两种不一样是正常的，它们统计的东西不同：

- `du -A -sk` 对每个文件先按块向上取整再相加，还算上了目录本身占的空间，856 个文件每个多算不到 1 KB，比精确值多了约 250 KB。
- `du -sk` 不加 `-A` 统计的是**占用的磁盘块**：一个 100 字节的文件也要占一整个块，所以比文件的实际长度之和大。

这个程序里还有一个设计上的选择：`add()` 只放在 `Folder` 里，`load` 拿到的是 `Folder` 的指针，可以直接调用。这是因为**建树的时候已经知道每一项是文件还是目录**（`is_directory()`）。下一节讨论 `add()` 应该放在哪里。

## 4. 透明还是安全：`add()` 放在哪里

`size()` 对叶子和容器都有意义，`add()` 呢？给一个文件「添加子节点」是没有意义的。GoF 讨论了两种做法：

- **透明**（transparency）：把 `add()` 等管理子节点的函数也放在 `Component` 里，叶子提供一个什么也不做（或者报错）的版本。好处是接口完全一致，调用者永远不需要知道手里是什么；坏处是对叶子调用 `add()` 能编译通过。
- **安全**（safety）：`add()` 只放在容器里。对叶子调用 `add()` 编译不过；代价是调用者必须先知道手里的是容器。

透明的写法，文件管理器里把文件拖到另一个文件上：

```cpp
// 《组合》：透明的设计——add() 放在基类，文件也有 add()，往文件里加东西编译能过，运行时悄悄失败
#include <iostream>
#include <memory>
#include <string>
#include <vector>

class Component {
public:
    explicit Component(std::string name) : name_(std::move(name)) {}
    virtual ~Component() = default;
    const std::string& name() const { return name_; }
    virtual long long size() const = 0;
    // 透明：管理子节点的接口也放在基类里，客户代码不需要知道手里的是文件还是文件夹
    virtual bool add(std::unique_ptr<Component>) { return false; }   // 默认：加不进去
    virtual int childCount() const { return 0; }
private:
    std::string name_;
};

class File : public Component {
public:
    File(std::string name, long long bytes) : Component(std::move(name)), bytes_(bytes) {}
    long long size() const override { return bytes_; }
private:
    long long bytes_;
};

class Folder : public Component {
public:
    using Component::Component;
    bool add(std::unique_ptr<Component> c) override { children_.push_back(std::move(c)); return true; }
    int childCount() const override { return static_cast<int>(children_.size()); }
    long long size() const override {
        long long sum = 0;
        for (const auto& c : children_) sum += c->size();
        return sum;
    }
private:
    std::vector<std::unique_ptr<Component>> children_;
};

int main() {
    std::vector<std::unique_ptr<Component>> items;      // 文件管理器里列出来的一项项
    items.push_back(std::make_unique<Folder>("src"));
    items.push_back(std::make_unique<File>("README.md", 2048));

    // 用户把 main.cpp 拖到第 1 项上，又把 notes.txt 拖到第 2 项上
    bool ok1 = items[0]->add(std::make_unique<File>("main.cpp", 12288));
    bool ok2 = items[1]->add(std::make_unique<File>("notes.txt", 4096));   // 编译通过，运行时什么也没发生
    std::cout << items[0]->name() << "：add 返回 " << ok1 << "，" << items[0]->childCount() << " 个子节点\n";
    std::cout << items[1]->name() << "：add 返回 " << ok2 << "，" << items[1]->childCount() << " 个子节点\n";
    std::cout << "总大小 " << (items[0]->size() + items[1]->size()) / 1024 << " KB（notes.txt 的 4 KB 不见了）\n";
}
```

```text
src：add 返回 1，1 个子节点
README.md：add 返回 0，0 个子节点
总大小 14 KB（notes.txt 的 4 KB 不见了）
```

编译没有任何警告，`notes.txt` 却悄悄丢了：`add()` 返回了 `false`，调用的人没检查，`unique_ptr` 被销毁。安全的写法，同样的两行（`safe_ce.cpp`）：

<!-- run: off -->
```cpp
    readme.add(std::make_unique<File>("notes.txt", 4096));    // 编译错误：File 没有 add

    Component& item = src;                                    // 只拿着基类引用时，也不能 add
    item.add(std::make_unique<File>("player.cpp", 30720));    // 编译错误：Component 没有 add
```

```text
clang++：safe_ce.cpp:40:12: error: no member named 'add' in 'File'
         safe_ce.cpp:43:10: error: no member named 'add' in 'Component'
g++：    safe_ce.cpp:40:12: error: 'class File' has no member named 'add'
         safe_ce.cpp:43:10: error: 'class Component' has no member named 'add'
```

第一个错误正是我们想要的；第二个错误是安全写法的代价：`item` 明明是个文件夹，只因为手里拿的是 `Component&`，就不能 `add`。这时可以在 `Component` 里加一个 `asFolder()`（GoF 叫 `GetComposite()`），容器返回自己，叶子返回空指针：

<!-- run: off -->
```cpp
class Component {
public:
    virtual Folder* asFolder() { return nullptr; }       // 叶子：不是容器
};
class Folder : public Component {
public:
    Folder* asFolder() override { return this; }
};

if (Folder* f = item.asFolder()) f->add(std::make_unique<File>("player.cpp", 30720));
else std::cout << item.name() << " 不是文件夹\n";
```

比起透明写法里一个可能被忽略的 `bool` 返回值，这里「不是文件夹怎么办」写在了调用者的眼前，没法假装没看见。它比 `dynamic_cast<Folder*>` 好的地方是：以后加了 `Archive` 这种「也算容器」的类，只要让它的 `asFolder()` 返回自己，调用者不用改。

| | 透明 | 安全 |
| --- | --- | --- |
| `add()` 放在 | `Component` | 只在容器里 |
| 对叶子调用 `add()` | 编译通过，运行时失败（返回 `false`、抛异常或什么也不做） | 编译错误 |
| 调用者需要区分叶子和容器吗 | 不需要 | 添加子节点时需要（用 `asFolder()`） |
| 例子 | GoF 原书的写法；很多 GUI 库的控件都有 `addChild` | `std::filesystem`（`directory_entry` 和目录迭代器分开） |

在 C++ 里我更倾向于**安全**的写法：能在编译期发现的错误，就不要留到运行时。大多数时候，像第 3 节那样建树的代码本来就知道自己在创建什么，用到 `asFolder()` 的地方并不多。

## 5. 复制一棵树：深拷贝

《原型模式》的练习 3 留了一个问题：`Group` 里可以再套 `Group`，怎么实现 `clone()`？那就是一棵组合树。`unique_ptr` 不能拷贝，所以 `Folder` 默认的拷贝构造函数被删除了，要自己写：逐个克隆子节点，子节点是文件夹时，它的 `clone()` 又会调用它自己的拷贝构造函数，自然地递归下去：

```cpp
// 《组合》：复制一棵树——容器的拷贝构造函数逐个克隆子节点，Folder 里套 Folder 时自然递归
#include <iostream>
#include <memory>
#include <string>
#include <vector>

class Component {
public:
    explicit Component(std::string name) : name_(std::move(name)) {}
    virtual ~Component() = default;
    const std::string& name() const { return name_; }
    virtual long long size() const = 0;
    virtual std::unique_ptr<Component> clone() const = 0;   // 原型模式：只拿着基类指针也能复制
private:
    std::string name_;
};

class File : public Component {
public:
    File(std::string name, long long bytes) : Component(std::move(name)), bytes_(bytes) {}
    long long size() const override { return bytes_; }
    std::unique_ptr<Component> clone() const override { return std::make_unique<File>(*this); }
private:
    long long bytes_;
};

class Folder : public Component {
public:
    using Component::Component;
    Folder(const Folder& other) : Component(other) {        // 深拷贝：unique_ptr 不能拷贝，逐个克隆子节点
        for (const auto& c : other.children_) children_.push_back(c->clone());
    }
    Folder& operator=(const Folder&) = delete;               // 赋值留作练习 4
    std::unique_ptr<Component> clone() const override { return std::make_unique<Folder>(*this); }

    template <class T, class... Args>
    T& add(Args&&... args) {
        children_.push_back(std::make_unique<T>(std::forward<Args>(args)...));
        return static_cast<T&>(*children_.back());
    }
    long long size() const override {
        long long sum = 0;
        for (const auto& c : children_) sum += c->size();
        return sum;
    }
    int count() const { return static_cast<int>(children_.size()); }
private:
    std::vector<std::unique_ptr<Component>> children_;
};

class Archive : public Folder {
public:
    Archive(std::string name, long long bytes) : Folder(std::move(name)), bytes_(bytes) {}
    long long size() const override { return bytes_; }
    // 默认的拷贝构造函数会调用 Folder 的深拷贝；clone() 仍然要重写，否则复制出来的是一个 Folder
    std::unique_ptr<Component> clone() const override { return std::make_unique<Archive>(*this); }
private:
    long long bytes_;
};

int main() {
    Folder templ("project");                                 // 模板项目
    templ.add<File>("README.md", 2048);
    auto& src = templ.add<Folder>("src");
    src.add<File>("main.cpp", 12288);
    auto& assets = templ.add<Folder>("assets");
    auto& zip = assets.add<Archive>("logs.zip", 1048576);
    zip.add<File>("decode.log", 6291456);
    zip.add<File>("encode.log", 4194304);

    Folder mine = templ;                                     // 从模板新建一个项目
    mine.add<File>("notes.txt", 4096);                       // 只改新项目
    std::cout << "模板：" << templ.count() << " 项，" << templ.size() / 1024 << " KB\n";
    std::cout << "新项目：" << mine.count() << " 项，" << mine.size() / 1024 << " KB\n";

    const Component& picked = zip;                           // 只拿着基类引用
    auto copy = picked.clone();
    std::cout << "克隆 " << copy->name() << "：" << copy->size() / 1024 << " KB\n";
}
```

```text
模板：3 项，1038 KB
新项目：4 项，1042 KB
克隆 logs.zip：1024 KB
```

深拷贝之后，往新项目里加文件，模板不受影响。只拿着基类引用 `picked` 的时候，`clone()` 复制出来的仍然是一个压缩包（1024 KB）。

`Archive` 的拷贝构造函数用的是编译器生成的默认版本，它会调用 `Folder` 的深拷贝，所以子节点也被复制了。但 `clone()` 必须重写：如果忘了，`Archive` 继承来的 `Folder::clone()` 会复制出一个普通的 `Folder`，大小从「压缩后的 1 MB」变成「子节点之和 10 MB」。实测把 `Archive::clone()` 删掉：

```text
模板：3 项，1038 KB
新项目：4 项，10258 KB
克隆 logs.zip：10240 KB
```

Clang 和 GCC 开着 `-Wall -Wextra` 都没有任何警告（GCC 加上 `-Wsuggest-override` 也没有：`Archive` 根本没有声明 `clone()`，没有什么可提示的）。这就是《原型模式》第 3 节「孙类忘了重写 `clone()`」的坑，在组合树里它更隐蔽：复制出来的树结构完全正确，只有大小不对。用《原型模式》的 CRTP 自动生成 `clone()` 可以避免：原型那篇说 `Group` 用不了 `Cloneable`，是因为它没有拷贝构造函数；现在 `Folder` 有了深拷贝的拷贝构造函数，写成 `class Archive : public Cloneable<Archive, Folder>` 就能自动得到正确的 `clone()`（实测结果和手写的一样）。

## 6. 树太深：递归会把栈用完

组合模式的操作天然是递归的：`Folder::size()` 调用子节点的 `size()`，每深一层，调用栈上就多一帧。C/C++ 专栏的《函数》测过，macOS 默认的栈是 8 MB，递归太深会栈溢出。树能有多深？下面这个程序造一条链：`root/d/d/d/…`，每层一个文件夹，最底下一个文件（`Component`、`File` 和第 2 节一样，省略了）：

<!-- run: off -->
```cpp
// 《组合》：一棵很深的树——递归的 size() 和递归的析构都会把栈用完
//   ./deep_tree 深度          先递归计算大小，再销毁整棵树；看程序在哪一步崩溃
//   ./deep_tree 深度 nosize   不计算大小，只建好再销毁

// ……

class Folder : public Component {
public:
    using Component::Component;
    Folder& addFolder(std::string name) {
        children_.push_back(std::make_unique<Folder>(std::move(name)));
        return static_cast<Folder&>(*children_.back());
    }
    void addFile(std::string name, long long bytes) { children_.push_back(std::make_unique<File>(std::move(name), bytes)); }
    long long size() const override {
        long long sum = 0;
        for (const auto& c : children_) sum += c->size();   // 每深一层，就多一层函数调用
        return sum;
    }
private:
    std::vector<std::unique_ptr<Component>> children_;
};

int main(int argc, char** argv) {
    int depth = argc > 1 ? std::atoi(argv[1]) : 100000;
    {
        auto root = std::make_unique<Folder>("root");
        Folder* cur = root.get();
        for (int i = 0; i < depth; ++i) cur = &cur->addFolder("d");   // 一层套一层：root/d/d/d/…
        cur->addFile("leaf.txt", 42);
        std::printf("深度 %d：建好了\n", depth);
        std::fflush(stdout);
        if (argc < 3 || std::strcmp(argv[2], "nosize") != 0) std::printf("size() = %lld\n", root->size());
        std::fflush(stdout);
    }                                                    // root 在这里销毁：~Folder → ~vector → ~unique_ptr → ~Folder → …
    std::printf("销毁完成\n");
}
```

用二分法找出程序能正常结束的最大深度（误差在 500 层以内；`nosize` 表示不调用 `size()`，只建树和销毁）：

| 编译器 | 优化 | `size()` 最深 | 只销毁，最深 |
| --- | --- | --- | --- |
| Apple clang 21 | `-O0` | 约 13.0 万层（每层约 64 字节） | **约 1.47 万层**（每层约 570 字节） |
| Apple clang 21 | `-O2` | 约 17.4 万层（每层约 48 字节） | 约 17.4 万层 |
| GCC 13 | `-O0` | 约 10.4 万层（每层约 80 字节） | **约 2.49 万层**（每层约 340 字节） |
| GCC 13 | `-O2` | 约 17.4 万层（每层约 48 字节） | 约 17.4 万层 |

<figure class="diagram">
<svg viewBox="0 0 720 200" role="img" aria-label="8 MB 的栈能撑多少层：递归 size() 在 Clang -O0 约 13.0 万层、-O2 约 17.4 万层，GCC -O0 约 10.4 万层、-O2 约 17.4 万层；只销毁树时，Clang -O0 约 1.47 万层、GCC -O0 约 2.49 万层就栈溢出，-O2 都约 17.4 万层"><g style="font-family:var(--mono)"><text x="120.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">递归 size()，最深（万层）</text><text x="420.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">只销毁整棵树，最深（万层）</text><text x="110.0" y="51.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">Clang -O0</text><rect x="120" y="36" width="134.1" height="20" rx="2" style="fill:var(--link)"/><text x="260.1" y="51.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">13.04</text><rect x="420" y="36" width="15.1" height="20" rx="2" style="fill:var(--danger)"/><text x="441.1" y="51.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">1.47</text><text x="110.0" y="87.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">Clang -O2</text><rect x="120" y="72" width="178.8" height="20" rx="2" style="fill:var(--accent)"/><text x="304.8" y="87.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">17.38</text><rect x="420" y="72" width="178.8" height="20" rx="2" style="fill:var(--accent)"/><text x="604.8" y="87.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">17.38</text><text x="110.0" y="123.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">GCC -O0</text><rect x="120" y="108" width="107.5" height="20" rx="2" style="fill:var(--link)"/><text x="233.5" y="123.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">10.45</text><rect x="420" y="108" width="25.6" height="20" rx="2" style="fill:var(--danger)"/><text x="451.6" y="123.0" text-anchor="start" style="fill:var(--danger);font-size:11.5px;font-weight:700">2.49</text><text x="110.0" y="159.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">GCC -O2</text><rect x="120" y="144" width="179.3" height="20" rx="2" style="fill:var(--accent)"/><text x="305.3" y="159.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">17.43</text><rect x="420" y="144" width="179.3" height="20" rx="2" style="fill:var(--accent)"/><text x="605.3" y="159.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">17.43</text><text x="10.0" y="186.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">深度为 N 的链 root/d/d/…，栈 8 MB；二分查找，误差 500 层以内；Apple clang 21、GCC 13</text></g></svg>
</figure>

最意外的是最后一列：**根本不调用 `size()`，只是让树离开作用域**，Debug 构建下一万多层就崩溃了。销毁也是递归的：`~Folder` 销毁 `children_`，`vector` 的析构函数销毁每个 `unique_ptr`，`unique_ptr` 的析构函数 `delete` 子节点，又进入下一个 `~Folder`……不开优化时，这一层里的每个函数（`~vector`、`~unique_ptr`、`default_delete::operator()`，还有标准库内部的几个辅助函数）都是独立的一帧，一层要好几百字节。开了 `-O2` 后它们被内联成一个函数，每层只要 48 字节。CMake 默认的 Debug 构建就是 `-O0`，开发时反而更容易崩。

真实的文件系统不会这么深（路径长度有上限，macOS 是 1024 字节），但**来自外部输入的树**深度往往不可控：嵌套的 JSON、XML，故意构造的、一层套一层的文件。只要深度由输入决定，递归就可能被一个特殊的输入弄崩溃。解决办法是把递归改成循环，自己用一个 `vector` 当栈，它在堆上，大小只受内存限制（`File` 和 `main` 省略了，`main` 和上面一样建一条链，再计时）：

<!-- run: off -->
```cpp
class Component {
public:
    explicit Component(std::string name) : name_(std::move(name)) {}
    virtual ~Component() = default;
    virtual long long size() const = 0;
    virtual Folder* asFolder() { return nullptr; }   // 是文件夹就返回自己，否则返回空（GoF 的 GetComposite）
    virtual const Folder* asFolder() const { return nullptr; }
private:
    std::string name_;
};

// ……

class Folder : public Component {
public:
    using Component::Component;
    ~Folder() override {
        // 把子孙节点一个个挪进 pending 再销毁：每个节点被销毁时已经没有子节点了，析构不会一层层递归下去
        std::vector<std::unique_ptr<Component>> pending = std::move(children_);
        while (!pending.empty()) {
            std::unique_ptr<Component> node = std::move(pending.back());
            pending.pop_back();
            if (Folder* f = node->asFolder()) {
                for (auto& c : f->children_) pending.push_back(std::move(c));
                f->children_.clear();                    // 清空：它自己的析构函数就没有子节点要处理了
            }
        }                                                // node 在这里销毁
    }
    Folder* asFolder() override { return this; }
    const Folder* asFolder() const override { return this; }
    Folder& addFolder(std::string name) {
        children_.push_back(std::make_unique<Folder>(std::move(name)));
        return static_cast<Folder&>(*children_.back());
    }
    void addFile(std::string name, long long bytes) { children_.push_back(std::make_unique<File>(std::move(name), bytes)); }
    long long size() const override {
        long long sum = 0;
        std::vector<const Component*> stack{this};       // 待访问的节点放在堆上的 vector 里，而不是调用栈上
        while (!stack.empty()) {
            const Component* n = stack.back();
            stack.pop_back();
            if (const Folder* f = n->asFolder())
                for (const auto& c : f->children_) stack.push_back(c.get());
            else
                sum += n->size();                        // 叶子：直接调用它的 size()
        }
        return sum;
    }
private:
    std::vector<std::unique_ptr<Component>> children_;
};
```

```text
深度 1000000：size() = 42，建树 48 ms，计算 4 ms
销毁完成，用了 17 ms
深度 10000000：size() = 42，建树 182 ms，计算 86 ms
销毁完成，用了 145 ms
```

（`-O2`；GCC 13 的结果相同，耗时略有差别。）1000 万层的链，内存占用约 800 MB，但不会栈溢出。两处改动：

- **`size()` 用显式的栈**：从栈里弹出一个节点，是容器就把它的子节点压进去，是叶子就累加它的大小。这里用到了第 4 节的 `asFolder()`。
- **析构函数也要改**：把子孙节点一个个挪到 `pending` 里再销毁，每个节点被销毁时已经没有子节点了，就不会一层层递归下去。

我第一次写析构函数时漏了 `f->children_.clear()`，结果程序崩溃（退出码 139）。被挪走的 `unique_ptr` 变成了空指针，但还留在 `children_` 里；节点销毁时，它自己的 `~Folder` 又把这些空指针挪进一个新的 `pending`，对空指针调用 `asFolder()` 就崩溃了。

显式栈的代价是代码变复杂了，还要破坏一点封装（需要 `asFolder()` 和访问子节点）。所以一般的做法是：树的深度有可靠的上限（比如由程序自己生成的 UI 控件树），就用递归，清楚直观；深度由外部输入决定，就改成显式栈，或者在解析时限制最大深度。

## 7. 另一棵树：MP4 的 box

音视频专栏的《MP4 封装格式》说过，MP4 由一层套一层的 box 组成：`moov` 里有 `trak`，`trak` 里有 `mdia`……那篇文章的 C 程序是边解析边打印。用组合模式把它读成一棵对象树，就可以在树上做各种检查。比如：容器 box 的长度应该**等于自己的头加上所有子 box 的长度**，这正是 `Folder::size()` 那样的递归求和：

<!-- run: off -->
```cpp
// 《组合》：MP4 的 box 树也是组合结构——容器 box 的长度应该等于 8 字节的头加上所有子 box 的长度
//   ./mp4_tree bbb-10s.mp4
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

class Box {
public:
    Box(std::string type, uint64_t declared) : type_(std::move(type)), declared_(declared) {}
    virtual ~Box() = default;
    const std::string& type() const { return type_; }
    uint64_t declared() const { return declared_; }          // 文件里写的长度
    virtual uint64_t size() const = 0;                       // 按结构算出来的长度
    virtual void print(int depth, int /*maxDepth*/) const {
        std::printf("%*s%-4s %8llu 字节\n", depth * 2, "", type_.c_str(), (unsigned long long)size());
    }
    virtual int check() const { return 0; }                  // 返回长度对不上的容器个数
private:
    std::string type_;
    uint64_t declared_;
};

class LeafBox : public Box {                                 // 叶子：内容是数据（mdat、stsz……）
public:
    using Box::Box;
    uint64_t size() const override { return declared(); }
};

class ContainerBox : public Box {                            // 容器：内容是子 box（moov、trak……）
public:
    ContainerBox(std::string type, uint64_t declared, uint64_t header) : Box(std::move(type), declared), header_(header) {}
    void add(std::unique_ptr<Box> b) { children_.push_back(std::move(b)); }
    uint64_t size() const override {
        uint64_t sum = header_;                              // 自己的头：通常是 4 字节长度 + 4 字节类型
        for (const auto& c : children_) sum += c->size();
        return sum;
    }
    void print(int depth, int maxDepth) const override {
        Box::print(depth, maxDepth);
        if (depth + 1 < maxDepth)
            for (const auto& c : children_) c->print(depth + 1, maxDepth);
    }
    int check() const override {
        int bad = size() != declared();
        for (const auto& c : children_) bad += c->check();
        return bad;
    }
private:
    uint64_t header_;
    std::vector<std::unique_ptr<Box>> children_;
};

static uint32_t be32(const unsigned char* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static bool isContainer(const char* t) {
    for (const char* c : {"moov", "trak", "mdia", "minf", "stbl", "dinf", "edts", "mvex", "moof", "traf"})
        if (std::memcmp(t, c, 4) == 0) return true;
    return false;
}

// 解析 [begin, end) 范围内的一串 box，加到 parent 里
static void parse(const std::vector<unsigned char>& buf, size_t begin, size_t end, ContainerBox& parent, int& count) {
    for (size_t pos = begin; pos + 8 <= end;) {
        uint64_t size = be32(&buf[pos]);
        const char* type = reinterpret_cast<const char*>(&buf[pos + 4]);
        size_t header = 8;
        if (size == 1) { size = (uint64_t)be32(&buf[pos + 8]) << 32 | be32(&buf[pos + 12]); header = 16; }
        if (size == 0) size = end - pos;
        if (size < header || pos + size > end) break;
        ++count;
        if (isContainer(type)) {
            auto c = std::make_unique<ContainerBox>(std::string(type, 4), size, header);
            parse(buf, pos + header, pos + size, *c, count);
            parent.add(std::move(c));
        } else {
            parent.add(std::make_unique<LeafBox>(std::string(type, 4), size));
        }
        pos += size;
    }
}

int main(int argc, char** argv) {
    std::ifstream in(argc > 1 ? argv[1] : "bbb-10s.mp4", std::ios::binary);
    std::vector<unsigned char> buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ContainerBox file("文件", buf.size(), 0);               // 整个文件就是一个没有头的容器
    int count = 0;
    parse(buf, 0, buf.size(), file, count);
    file.print(0, 4);
    std::printf("共 %d 个 box；长度对不上的容器：%d 个\n", count, file.check());
}
```

```text
文件  2994711 字节
  ftyp       32 字节
  moov     7926 字节
    mvhd      108 字节
    trak     4024 字节
      tkhd       92 字节
      edts       36 字节
      mdia     3888 字节
    trak     3689 字节
      tkhd       92 字节
      edts       36 字节
      mdia     3553 字节
    udta       97 字节
  free        8 字节
  mdat  2986745 字节
共 44 个 box；长度对不上的容器：0 个
```

整个文件 2994711 字节，`moov` 7926 字节，和《MP4 封装格式》里的 box 树一致；44 个 box 里，每个容器 box 写在文件里的长度，都等于按结构算出来的长度。如果一个文件在写入时出错（比如某个容器的长度没有更新），`check()` 会指出来。

这里的叶子 `LeafBox` 和容器 `ContainerBox` 都继承 `Box`，`print`、`check` 都是一个虚函数调用，容器负责往下传。整棵树的根是一个「没有头的容器」（`header_` 是 0），这样整个文件和 `moov` 用的是同一套代码。

## 8. 组合和其他模式

| 模式 | 和组合的关系 |
| --- | --- |
| 原型 | 复制一棵组合树要逐个克隆子节点（第 5 节） |
| 装饰器（3.4） | 结构很像：装饰器也实现和被包装对象相同的接口、持有它。区别是装饰器只有**一个**子节点，目的是**增加功能**；组合有**任意多个**子节点，目的是**表示整体和部分** |
| 享元（3.6） | 树里有大量相同的叶子（比如文档里的每个字符）时，可以共用叶子对象，但叶子就不能保存自己在树里的位置了 |
| 迭代器（第 4 章） | 把「怎么遍历这棵树」（前序、后序、显式栈）从树的类里拿出来 |
| 访问者（第 4 章） | 组合模式每加一个操作（`countFiles`、`print`、`collect`……），所有类都要改。访问者把操作放到类外面，适合节点种类稳定、操作经常增加的场合 |
| 责任链（第 4 章） | 子节点处理不了的请求，沿着父节点往上传，比如 GUI 里的事件冒泡 |

最后一行提到 GUI：控件树是组合模式最经典的应用。窗口里有面板，面板里有按钮、文本框，也可以再有面板；「绘制」「计算大小」「找到鼠标点中的控件」都是从根节点递归下去。浏览器的 DOM 也是这样一棵树。

## 9. 小结

- **组合模式**：叶子和容器实现同一个接口，容器把操作递归地交给子节点。调用者不需要区分单个对象和一组对象
- 不用组合模式：每个操作都要判断节点种类、自己递归。实测加一种节点时漏改一处，项目大小从 1116 KB 算成了 10332 KB
- C++ 里用 `std::vector<std::unique_ptr<Component>>` 持有子节点：所有权清楚，父节点销毁时子节点自动销毁
- 实测：用 `std::filesystem` 读成组合树，`source/` 目录 856 个文件 115143463 字节，和 `find`、GNU `du -sb` 一致；MP4 的 44 个 box，每个容器的长度都等于子 box 之和
- **透明**（`add()` 放在基类）接口一致，但对叶子 `add()` 只能在运行时发现；**安全**（`add()` 只放在容器）能在编译期发现，需要时用 `asFolder()` 取得容器
- 深拷贝：容器的拷贝构造函数逐个 `clone()` 子节点；子类忘了重写 `clone()`，两个编译器都不报警告
- 递归遍历和**递归析构**都受栈大小限制：Debug 构建下销毁一条约 1.5 万层（Clang）/ 2.5 万层（GCC）的链就会栈溢出。深度由外部输入决定时，改成显式栈
- 三个「组合」：组合优于继承（持有对象）、UML 组合关系（同生共死）、组合模式（树形结构、统一接口）

## 10. 练习

1. 用 `unique_ptr` 持有子节点的组合树，能不能出现环（一个文件夹直接或间接地包含自己）？能不能让同一个文件同时出现在两个文件夹里？真实的文件系统里，硬链接就是「同一个文件出现在两个地方」，第 3 节的 `du_tree` 遇到硬链接会怎样？

2. 给组合树加一个操作：列出所有大于某个大小的文件，带上完整的路径（如 `project/assets/video/bbb-10s.mp4`）。

3. 用显式的栈实现目录树的打印，输出（顺序和缩进）要和递归版完全一样。

4. 第 5 节的 `Folder` 禁止了拷贝赋值。给它补上拷贝赋值和移动操作，要求自赋值（`a = a`）也正确。

<details>
<summary>点击查看答案</summary>

1. **不能出现环，也不能共享**。`unique_ptr` 表示唯一的所有权：一个节点只能被一个 `unique_ptr` 拥有，要把它加进另一个文件夹，必须 `std::move` 过去，原来的位置就空了。要形成环，一个文件夹就得拥有它的祖先，而祖先已经被别人拥有了。（硬要用 `release()` 再造一个 `unique_ptr` 可以做到，但那是两个 `unique_ptr` 拥有同一个对象，析构时会重复 `delete`。）所以只要不 `release()`，`unique_ptr` 本身就保证了结构是一棵树。

   如果确实需要共享（同一个对象出现在两处），就要改用 `shared_ptr`，结构变成了一个有向无环图。这时递归求和会把共享的节点算两次。实测：建一个目录，放一个 1 MB 的文件，再用 `ln` 给它建一个硬链接：

   ```text
   $ du -A -sk hl
   1025	hl
   $ ./du_tree hl
   hl：2 个文件，1 个目录，共 2097152 字节（2048 KB）
     video.mp4           1024 KB      1 个文件
     backup.mp4          1024 KB      1 个文件
   ```

   `du` 记住了见过的文件（设备号和 inode 号），同一个文件只算一次；我们的组合树把它当成两个文件，算了 2 MB。要和 `du` 一致，可以在 `load` 时用 POSIX 的 `stat` 取得 inode 号，放进一个 `std::set`，见过的就跳过（`std::filesystem` 能告诉你 `hard_link_count()`，但不提供 inode 号）。

2. 叶子判断自己，容器把带上自己名字的路径传给子节点：

   ```cpp
   // 《组合》练习 2：列出所有大于某个大小的文件，带上完整路径
   #include <iostream>
   #include <memory>
   #include <string>
   #include <vector>

   struct Found {
       std::string path;
       long long bytes;
   };

   class Component {
   public:
       explicit Component(std::string name) : name_(std::move(name)) {}
       virtual ~Component() = default;
       const std::string& name() const { return name_; }
       virtual long long size() const = 0;
       // prefix 是父目录的路径；叶子判断自己，容器把带上自己名字的路径传给子节点
       virtual void collect(const std::string& prefix, long long minBytes, std::vector<Found>& out) const = 0;
   private:
       std::string name_;
   };

   class File : public Component {
   public:
       File(std::string name, long long bytes) : Component(std::move(name)), bytes_(bytes) {}
       long long size() const override { return bytes_; }
       void collect(const std::string& prefix, long long minBytes, std::vector<Found>& out) const override {
           if (bytes_ >= minBytes) out.push_back({prefix + name(), bytes_});
       }
   private:
       long long bytes_;
   };

   class Folder : public Component {
   public:
       using Component::Component;
       template <class T, class... Args>
       T& add(Args&&... args) {
           children_.push_back(std::make_unique<T>(std::forward<Args>(args)...));
           return static_cast<T&>(*children_.back());
       }
       long long size() const override {
           long long sum = 0;
           for (const auto& c : children_) sum += c->size();
           return sum;
       }
       void collect(const std::string& prefix, long long minBytes, std::vector<Found>& out) const override {
           for (const auto& c : children_) c->collect(prefix + name() + "/", minBytes, out);
       }
   private:
       std::vector<std::unique_ptr<Component>> children_;
   };

   int main() {
       Folder root("project");
       root.add<File>("README.md", 2048);
       auto& src = root.add<Folder>("src");
       src.add<File>("main.cpp", 12288);
       src.add<File>("player.cpp", 30720);
       auto& assets = root.add<Folder>("assets");
       assets.add<File>("logo.png", 49152);
       assets.add<Folder>("video").add<File>("bbb-10s.mp4", 2994711);

       std::vector<Found> big;
       root.collect("", 20 * 1024, big);                 // 20 KB 以上的文件
       for (const auto& f : big) std::cout << f.path << "  " << f.bytes / 1024 << " KB\n";
   }
   ```

   ```text
   project/src/player.cpp  30 KB
   project/assets/logo.png  48 KB
   project/assets/video/bbb-10s.mp4  2924 KB
   ```

   路径是在递归**往下走**的时候拼出来的（参数 `prefix`），大小是在递归**往回返**的时候汇总的（返回值），这是树形递归常用的两个方向。

3. 栈里除了节点，还要记住它的深度；子节点要**倒着**压栈，第一个子节点才会最先弹出：

   ```cpp
   // 《组合》练习 3：用显式的栈打印整棵树，顺序和缩进要和递归版完全一样
   #include <iostream>
   #include <memory>
   #include <sstream>
   #include <string>
   #include <utility>
   #include <vector>

   class Folder;

   class Component {
   public:
       explicit Component(std::string name) : name_(std::move(name)) {}
       virtual ~Component() = default;
       const std::string& name() const { return name_; }
       virtual std::string label() const = 0;                   // 这一行显示的内容
       virtual const Folder* asFolder() const { return nullptr; }
   private:
       std::string name_;
   };

   class File : public Component {
   public:
       File(std::string name, long long bytes) : Component(std::move(name)), bytes_(bytes) {}
       std::string label() const override { return name() + "  " + std::to_string(bytes_ / 1024) + " KB"; }
   private:
       long long bytes_;
   };

   class Folder : public Component {
   public:
       using Component::Component;
       std::string label() const override { return name() + "/"; }
       const Folder* asFolder() const override { return this; }
       template <class T, class... Args>
       T& add(Args&&... args) {
           children_.push_back(std::make_unique<T>(std::forward<Args>(args)...));
           return static_cast<T&>(*children_.back());
       }
       const std::vector<std::unique_ptr<Component>>& children() const { return children_; }
   private:
       std::vector<std::unique_ptr<Component>> children_;
   };

   void printRecursive(const Component& c, std::ostream& os, int depth = 0) {
       os << std::string(depth * 2, ' ') << c.label() << '\n';
       if (const Folder* f = c.asFolder())
           for (const auto& child : f->children()) printRecursive(*child, os, depth + 1);
   }

   void printIterative(const Component& root, std::ostream& os) {
       std::vector<std::pair<const Component*, int>> stack{{&root, 0}};   // 节点和它的深度
       while (!stack.empty()) {
           auto [c, depth] = stack.back();
           stack.pop_back();
           os << std::string(depth * 2, ' ') << c->label() << '\n';
           if (const Folder* f = c->asFolder()) {
               const auto& ch = f->children();
               for (auto it = ch.rbegin(); it != ch.rend(); ++it)        // 倒着压栈，第一个子节点最先弹出
                   stack.push_back({it->get(), depth + 1});
           }
       }
   }

   int main() {
       Folder root("project");
       root.add<File>("README.md", 2048);
       auto& src = root.add<Folder>("src");
       src.add<File>("main.cpp", 12288);
       src.add<File>("player.cpp", 30720);
       auto& assets = root.add<Folder>("assets");
       assets.add<File>("logo.png", 49152);
       assets.add<Folder>("video").add<File>("bbb-10s.mp4", 2994711);

       std::ostringstream a, b;
       printRecursive(root, a);
       printIterative(root, b);
       std::cout << b.str() << (a.str() == b.str() ? "和递归版完全一样\n" : "和递归版不一样！\n");
   }
   ```

   ```text
   project/
     README.md  2 KB
     src/
       main.cpp  12 KB
       player.cpp  30 KB
     assets/
       logo.png  48 KB
       video/
         bbb-10s.mp4  2924 KB
   和递归版完全一样
   ```

   这是**前序遍历**（先访问自己，再访问子节点）。如果是求大小这样「需要子节点的结果才能算出自己」的操作，用显式栈写后序遍历会麻烦一些；第 6 节的 `size()` 能写得那么简单，是因为加法和顺序无关，只要把所有叶子加起来就行。

4. 用「拷贝并交换」：赋值运算符按值接收参数，复制（或移动）在传参时已经完成，函数里只需要交换：

   ```cpp
   // 《组合》练习 4：给 Folder 补上拷贝赋值和移动操作（拷贝并交换）
   #include <iostream>
   #include <memory>
   #include <string>
   #include <utility>
   #include <vector>

   class Component {
   public:
       explicit Component(std::string name) : name_(std::move(name)) {}
       virtual ~Component() = default;
       const std::string& name() const { return name_; }
       virtual long long size() const = 0;
       virtual std::unique_ptr<Component> clone() const = 0;
   protected:
       Component(const Component&) = default;
       Component& operator=(const Component&) = default;
       Component(Component&&) = default;
       Component& operator=(Component&&) = default;
   private:
       std::string name_;
   };

   class File : public Component {
   public:
       File(std::string name, long long bytes) : Component(std::move(name)), bytes_(bytes) {}
       long long size() const override { return bytes_; }
       std::unique_ptr<Component> clone() const override { return std::make_unique<File>(*this); }
   private:
       long long bytes_;
   };

   class Folder : public Component {
   public:
       using Component::Component;
       Folder(const Folder& other) : Component(other) {
           for (const auto& c : other.children_) children_.push_back(c->clone());
       }
       Folder(Folder&&) noexcept = default;              // 移动：直接接管子节点，不复制
       Folder& operator=(Folder other) noexcept {         // 拷贝并交换：按值传参时已经复制（或移动）好了
           Component::operator=(std::move(other));        // 名字等基类的成员
           children_.swap(other.children_);               // 换过来；原来的子节点随 other 一起销毁
           return *this;
       }
       std::unique_ptr<Component> clone() const override { return std::make_unique<Folder>(*this); }

       template <class T, class... Args>
       T& add(Args&&... args) {
           children_.push_back(std::make_unique<T>(std::forward<Args>(args)...));
           return static_cast<T&>(*children_.back());
       }
       long long size() const override {
           long long sum = 0;
           for (const auto& c : children_) sum += c->size();
           return sum;
       }
   private:
       std::vector<std::unique_ptr<Component>> children_;
   };

   int main() {
       Folder a("a"), b("b");
       a.add<File>("x.txt", 1000);
       b.add<Folder>("sub").add<File>("y.txt", 2000);

       a = b;                                             // 拷贝赋值：a 变成 b 的一份深拷贝
       b.add<File>("z.txt", 4000);                        // 再改 b，a 不受影响
       std::cout << a.name() << " " << a.size() << "，" << b.name() << " " << b.size() << '\n';

       Folder& same = a;
       a = same;                                          // 自赋值也安全
       std::cout << "自赋值之后 " << a.size() << '\n';

       Folder c = std::move(b);                           // 移动：不复制子节点
       std::cout << c.name() << " " << c.size() << '\n';
   }
   ```

   ```text
   b 2000，b 6000
   自赋值之后 2000
   b 6000
   ```

   - **自赋值**：`other` 是 `a` 的一份完整拷贝，交换之后旧的子节点随 `other` 销毁，`a` 的内容不变。不需要写 `if (this == &other)`。
   - **异常安全**：如果复制到一半失败（内存不足），异常在进入函数体之前抛出，`a` 还是原来的样子。
   - **移动**：`Folder(Folder&&) = default` 直接接管 `children_` 的缓冲区，不复制任何子节点。按值传参的 `operator=` 同时也是移动赋值：`a = std::move(b)` 时 `other` 是移动构造的。
   - 把 `Component` 的拷贝和移动声明成 `protected`，防止 `Component c = folder;` 这样的切片。
   - 赋值时 `Archive` 的 `bytes_` 怎么办？`Archive` 默认的拷贝赋值会先调用 `Folder::operator=`，再复制 `bytes_`，不需要另写。

</details>
