// 《迭代器模式》练习 3：给《组合模式》的文件树写一个前序遍历迭代器——内部用显式栈，调用方用 range-for 和 std::count_if
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct Node {
    std::string name;
    long long bytes = 0;                               // 文件大小；文件夹为 0
    std::vector<std::unique_ptr<Node>> children;       // 文件没有子节点
    Node* add(std::string n, long long b = 0) {
        children.push_back(std::make_unique<Node>(Node{std::move(n), b, {}}));
        return children.back().get();
    }
};

class PreorderRange {
public:
    explicit PreorderRange(const Node& root) : root_(&root) {}

    class iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = Node;
        using difference_type = std::ptrdiff_t;
        using pointer = const Node*;
        using reference = const Node&;

        iterator() = default;                          // end：栈是空的
        explicit iterator(const Node* root) { stack_.push_back({root, 0}); }
        reference operator*() const { return *stack_.back().node; }
        pointer operator->() const { return stack_.back().node; }
        int depth() const { return static_cast<int>(stack_.size()) - 1; }
        iterator& operator++() {
            // 有子节点就进入第一个；没有就往上找「还有下一个兄弟」的祖先
            if (!stack_.back().node->children.empty()) { stack_.push_back({stack_.back().node->children[0].get(), 0}); return *this; }
            while (!stack_.empty()) {
                stack_.pop_back();
                if (stack_.empty()) break;
                Frame& parent = stack_.back();
                if (++parent.next < parent.node->children.size()) { stack_.push_back({parent.node->children[parent.next].get(), 0}); break; }
            }
            return *this;
        }
        iterator operator++(int) { auto t = *this; ++*this; return t; }
        friend bool operator==(const iterator& a, const iterator& b) {
            if (a.stack_.empty() || b.stack_.empty()) return a.stack_.empty() == b.stack_.empty();
            return a.stack_.back().node == b.stack_.back().node;
        }
        friend bool operator!=(const iterator& a, const iterator& b) { return !(a == b); }

    private:
        struct Frame { const Node* node; std::size_t next; };   // next：当前在走第几个子节点
        std::vector<Frame> stack_;
    };
    iterator begin() const { return iterator(root_); }
    iterator end() const { return iterator(); }

private:
    const Node* root_;
};

int main() {
    Node root{"project", 0, {}};
    Node* media = root.add("media");
    media->add("bbb-10s.mp4", 2'000'000);
    media->add("bbb-10s.flv", 3'000'038);
    Node* src = root.add("src");
    src->add("main.cpp", 1'200);
    src->add("ring.h", 900);
    root.add("README.md", 300);

    PreorderRange all(root);
    for (auto it = all.begin(); it != all.end(); ++it)
        std::printf("%*s%s%s\n", it.depth() * 2, "", it->name.c_str(), it->children.empty() ? "" : "/");
    long long total = 0;
    for (const Node& n : all) total += n.bytes;
    auto files = std::count_if(all.begin(), all.end(), [](const Node& n) { return n.children.empty() && n.bytes > 0; });
    std::printf("%td 个文件，共 %lld 字节\n", files, total);
}
