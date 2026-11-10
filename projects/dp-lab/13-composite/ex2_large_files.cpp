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
