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
