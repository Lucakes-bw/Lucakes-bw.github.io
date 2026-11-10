// 《组合》：用显式的栈代替递归——size() 和析构函数都不再受调用栈大小的限制
//   ./deep_fixed 深度
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

class Folder;

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

int main(int argc, char** argv) {
    using Clock = std::chrono::steady_clock;
    int depth = argc > 1 ? std::atoi(argv[1]) : 1000000;
    auto t0 = Clock::now();
    {
        auto root = std::make_unique<Folder>("root");
        Folder* cur = root.get();
        for (int i = 0; i < depth; ++i) cur = &cur->addFolder("d");
        cur->addFile("leaf.txt", 42);
        auto t1 = Clock::now();
        long long s = root->size();
        auto t2 = Clock::now();
        std::printf("深度 %d：size() = %lld，建树 %lld ms，计算 %lld ms\n", depth, s,
                    (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count(),
                    (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count());
        t0 = Clock::now();
    }
    std::printf("销毁完成，用了 %lld ms\n",
                (long long)std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count());
}
