// 《代理模式》：不为每个类写一个代理——通用的 Lazy<T>，把「怎么创建」交给一个 lambda
#include <cstdio>
#include <functional>
#include <optional>
#include <string>

template <class T>
class Lazy {
public:
    explicit Lazy(std::function<T()> make) : make_(std::move(make)) {}
    T& get() {
        if (!value_) value_.emplace(make_());   // 第一次 get 时才创建，之后一直复用
        return *value_;
    }
    T* operator->() { return &get(); }          // 像指针一样用
    bool ready() const { return value_.has_value(); }

private:
    std::function<T()> make_;
    std::optional<T> value_;                    // 就地存放，不额外分配堆内存
};

struct Subtitle {
    explicit Subtitle(const std::string& path) : path(path) { std::printf("  解析字幕 %s（很慢）\n", path.c_str()); }
    int lines() const { return 1420; }
    std::string path;
};

int main() {
    Lazy<Subtitle> sub([] { return Subtitle("bbb.zh.srt"); });
    std::printf("打开视频，字幕已加载：%s\n", sub.ready() ? "是" : "否");
    std::printf("用户打开字幕：\n");
    std::printf("  共 %d 行\n", sub->lines());
    std::printf("再看一次：%d 行，已加载：%s\n", sub->lines(), sub.ready() ? "是" : "否");
    std::printf("sizeof(Lazy<Subtitle>) = %zu，sizeof(Subtitle) = %zu\n", sizeof(Lazy<Subtitle>), sizeof(Subtitle));
}
