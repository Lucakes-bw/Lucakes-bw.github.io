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
