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
