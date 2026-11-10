// 《享元模式》：字符串驻留——时间轴上 10 万个片段的滤镜描述只有 6 种，每种只存一份，片段里只放 string_view
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

static std::size_t g_allocs = 0, g_live = 0;
void* operator new(std::size_t n) {
    ++g_allocs;
    g_live += n;
    auto* h = static_cast<std::size_t*>(std::malloc(n + 16));
    if (!h) std::abort();
    *h = n;
    return reinterpret_cast<char*>(h) + 16;
}
void operator delete(void* p) noexcept {
    if (!p) return;
    auto* h = reinterpret_cast<std::size_t*>(static_cast<char*>(p) - 16);
    g_live -= *h;
    std::free(h);
}
void operator delete(void* p, std::size_t) noexcept { operator delete(p); }

// 驻留池：同样内容的字符串只存一份，返回指向它的 string_view
class StringPool {
public:
    std::string_view intern(std::string_view s) {
        auto it = pool_.find(s);  // 用 string_view 查找，不用为了查找先构造一个 std::string
        if (it != pool_.end()) return it->first;
        auto owned = std::make_unique<std::string>(s);
        std::string_view key = *owned;  // 键指向 unique_ptr 管的字符串，map 扩容时字符串不会搬家
        pool_.emplace(key, std::move(owned));
        return key;
    }
    std::size_t size() const { return pool_.size(); }

private:
    std::unordered_map<std::string_view, std::unique_ptr<std::string>> pool_;
};

const char* const kFilters[] = {
    "scale=1920:1080:flags=bicubic,format=yuv420p",
    "scale=1280:720:flags=bicubic,format=yuv420p",
    "crop=1920:800:0:140,scale=1920:1080,format=yuv420p",
    "eq=brightness=0.06:contrast=1.10:saturation=1.20",
    "fade=t=in:st=0:d=0.5,fade=t=out:st=4.5:d=0.5",
    "hflip,scale=1920:1080:flags=lanczos,format=yuv420p",
};

struct Clip {  // 不共享：每个片段自己存一份滤镜描述
    std::int64_t start_us, dur_us;
    std::string filter;
};
struct InternedClip {  // 驻留：片段里只放一个 string_view
    std::int64_t start_us, dur_us;
    std::string_view filter;
};

int main() {
    const int N = 100000;
    std::uint32_t s = 7;
    std::vector<int> which(N);  // 第 i 个片段用第几种滤镜
    for (auto& w : which) { s = s * 1664525u + 1013904223u; w = (s >> 8) % 6; }
    std::printf("sizeof(std::string) = %zu，sizeof(std::string_view) = %zu\n",
                sizeof(std::string), sizeof(std::string_view));

    std::size_t a0 = g_allocs, m0 = g_live;
    std::vector<Clip> clips;
    clips.reserve(N);
    for (int i = 0; i < N; ++i) clips.push_back({i * 40000LL, 40000, kFilters[which[i]]});
    std::printf("不共享：堆分配 %zu 次，占用 %.2f MB\n", g_allocs - a0, (g_live - m0) / 1048576.0);

    a0 = g_allocs, m0 = g_live;
    StringPool pool;
    std::vector<InternedClip> iclips;
    iclips.reserve(N);
    for (int i = 0; i < N; ++i) iclips.push_back({i * 40000LL, 40000, pool.intern(kFilters[which[i]])});
    std::printf("驻留：  堆分配 %zu 次，占用 %.2f MB（池里 %zu 个字符串）\n",
                g_allocs - a0, (g_live - m0) / 1048576.0, pool.size());

    // 找出所有用了第 2 种滤镜的片段：不共享时要比较字符串内容，驻留以后比较地址就够了
    using Clock = std::chrono::steady_clock;
    std::string target = kFilters[1];
    std::string_view itarget = pool.intern(target);
    int n1 = 0, n2 = 0;
    auto t0 = Clock::now();
    for (int r = 0; r < 100; ++r)
        for (auto& c : clips) n1 += (c.filter == target);
    auto t1 = Clock::now();
    for (int r = 0; r < 100; ++r)
        for (auto& c : iclips) n2 += (c.filter.data() == itarget.data());
    auto t2 = Clock::now();
    auto ns = [](auto a, auto b) { return std::chrono::duration<double, std::nano>(b - a).count() / (100.0 * N); };
    std::printf("按滤镜查找：比较内容 %.2f ns/个，比较地址 %.2f ns/个（都找到 %d 个）\n",
                ns(t0, t1), ns(t1, t2), n1 / 100 == n2 / 100 ? n1 / 100 : -1);
}
