// 《组合》：一棵很深的树——递归的 size() 和递归的析构都会把栈用完
//   ./deep_tree 深度          先递归计算大小，再销毁整棵树；看程序在哪一步崩溃
//   ./deep_tree 深度 nosize   不计算大小，只建好再销毁
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

class Component {
public:
    explicit Component(std::string name) : name_(std::move(name)) {}
    virtual ~Component() = default;
    virtual long long size() const = 0;
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
