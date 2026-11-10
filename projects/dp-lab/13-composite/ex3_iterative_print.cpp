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
