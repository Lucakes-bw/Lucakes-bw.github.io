// 《组合》：安全的设计——add() 只在文件夹里，往文件里加东西编译不过（故意编译失败）
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
    void add(std::unique_ptr<Component> c) { children_.push_back(std::move(c)); }   // 只有文件夹有
    long long size() const override {
        long long sum = 0;
        for (const auto& c : children_) sum += c->size();
        return sum;
    }
private:
    std::vector<std::unique_ptr<Component>> children_;
};

int main() {
    Folder src("src");
    File readme("README.md", 2048);
    src.add(std::make_unique<File>("main.cpp", 12288));
    readme.add(std::make_unique<File>("notes.txt", 4096));    // 编译错误：File 没有 add

    Component& item = src;                                    // 只拿着基类引用时，也不能 add
    item.add(std::make_unique<File>("player.cpp", 30720));    // 编译错误：Component 没有 add
}
