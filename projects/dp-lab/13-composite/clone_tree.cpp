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
