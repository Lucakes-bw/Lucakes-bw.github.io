// 《迭代器模式》：C++20 协程写生成器——按需解析内存里的 FLV tag，co_yield 一个就停一下（C++23 才有 std::generator，这里自己写一个最小的）
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <utility>
#include <vector>

template <class T>
class Generator {
public:
    struct promise_type {
        const T* value = nullptr;
        Generator get_return_object() { return Generator(std::coroutine_handle<promise_type>::from_promise(*this)); }
        std::suspend_always initial_suspend() noexcept { return {}; }   // 创建时不跑，等第一次 begin()
        std::suspend_always final_suspend() noexcept { return {}; }
        std::suspend_always yield_value(const T& v) noexcept { value = &v; return {}; }   // 交出一个值就暂停
        void return_void() {}
        void unhandled_exception() {}
    };
    struct sentinel {};
    class iterator {
    public:
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        explicit iterator(std::coroutine_handle<promise_type> h) : h_(h) {}
        const T& operator*() const { return *h_.promise().value; }
        iterator& operator++() { h_.resume(); return *this; }            // ++ 就是「让协程再跑到下一个 co_yield」
        void operator++(int) { ++*this; }
        bool operator==(sentinel) const { return h_.done(); }
    private:
        std::coroutine_handle<promise_type> h_;
    };
    iterator begin() { h_.resume(); return iterator(h_); }
    sentinel end() { return {}; }

    explicit Generator(std::coroutine_handle<promise_type> h) : h_(h) {}
    Generator(Generator&& o) noexcept : h_(std::exchange(o.h_, {})) {}
    ~Generator() { if (h_) h_.destroy(); }

private:
    std::coroutine_handle<promise_type> h_;
};

struct Tag { int type; std::uint32_t ts; std::size_t size; };

// 解析逻辑写成一个普通的循环；状态（读到哪了）就是协程里的局部变量 p
Generator<Tag> flvTags(const std::vector<unsigned char>& file) {
    std::size_t p = 13;                                     // 跳过 9 字节文件头 + PreviousTagSize0
    while (p + 11 <= file.size()) {
        const unsigned char* h = &file[p];
        Tag t{h[0], static_cast<std::uint32_t>(h[4] << 16 | h[5] << 8 | h[6]), static_cast<std::size_t>(h[1] << 16 | h[2] << 8 | h[3])};
        std::printf("  [解析 %zu 处]", p);
        co_yield t;
        p += 11 + t.size + 4;
    }
}

int main() {
    // 一个很小的 FLV：文件头 + 3 个 tag（视频 ts=0、音频 ts=0、视频 ts=40），数据都只有 2 字节
    std::vector<unsigned char> file = {'F', 'L', 'V', 1, 5, 0, 0, 0, 9, 0, 0, 0, 0};
    for (auto [type, ts] : {std::pair{9, 0}, std::pair{8, 0}, std::pair{9, 40}}) {
        unsigned char tag[] = {static_cast<unsigned char>(type), 0, 0, 2, 0, 0, static_cast<unsigned char>(ts), 0, 0, 0, 0, 0x17, 0, 0, 0, 0, 13};
        file.insert(file.end(), std::begin(tag), std::end(tag));
    }
    for (const Tag& t : flvTags(file)) {
        std::printf(" type=%d ts=%u\n", t.type, t.ts);
        if (t.type == 8) { std::printf("找到第一个音频包，停下：后面的 tag 不会再解析\n"); break; }
    }
}
