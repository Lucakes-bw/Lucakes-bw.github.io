// 《代理模式》：vector<bool>::reference 是一个代理对象——auto 推导出来的不是 bool
#include <cstdio>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <vector>

static std::size_t g_bytes = 0;                       // 统计 new 出去的总字节数
void* operator new(std::size_t n) { g_bytes += n; if (void* p = std::malloc(n)) return p; std::abort(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

int main() {
    std::vector<bool> key(8);                         // 8 个包是不是关键帧
    std::vector<int> pts(8);

    auto a = key[3];                                  // 推导出 vector<bool>::reference，不是 bool
    auto b = pts[3];                                  // 推导出 int，是一份拷贝
    a = true;
    b = 100;
    std::printf("a = %d，key[3] = %d\n", static_cast<int>(a), static_cast<int>(key[3]));   // key[3] 跟着变了
    std::printf("b = %d，pts[3] = %d\n", b, pts[3]);                                     // pts[3] 没变
    static_assert(std::is_same_v<decltype(a), std::vector<bool>::reference>);
    static_assert(!std::is_same_v<decltype(key[3]), bool&>);
    std::printf("sizeof(a) = %zu，sizeof(bool) = %zu\n", sizeof(a), sizeof(bool));

    bool c = key[4];                                  // 想要一份拷贝，就写明 bool
    c = true;
    std::printf("c = %d，key[4] = %d\n", static_cast<int>(c), static_cast<int>(key[4]));

    // 代理换来的好处：每个元素只占 1 位
    g_bytes = 0;
    std::vector<bool> bits(1'000'000);
    std::size_t bool_bytes = g_bytes;
    g_bytes = 0;
    std::vector<char> chars(1'000'000);
    std::printf("100 万个元素：vector<bool> %zu 字节，vector<char> %zu 字节\n", bool_bytes, g_bytes);
}
