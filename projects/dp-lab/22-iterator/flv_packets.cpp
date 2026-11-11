// 《迭代器模式》：遍历 FLV 文件里的包——一次全读进 vector，还是写一个按需读取的输入迭代器（比较内存和首包时间）
//   ffmpeg -i bbb-10s.mp4 -c copy bbb-10s.flv && ./flv_packets bbb-10s.flv
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <new>
#include <vector>

// ---- 统计 operator new：当前占用和峰值 ----
static std::size_t g_now = 0, g_peak = 0, g_count = 0;
void* operator new(std::size_t n) {
    auto* p = static_cast<std::size_t*>(std::malloc(n + 16));
    if (!p) throw std::bad_alloc();
    *p = n;
    g_now += n; g_peak = std::max(g_peak, g_now); ++g_count;
    return reinterpret_cast<char*>(p) + 16;
}
void operator delete(void* q) noexcept {
    if (!q) return;
    auto* p = reinterpret_cast<std::size_t*>(static_cast<char*>(q) - 16);
    g_now -= *p;
    std::free(p);
}
void operator delete(void* q, std::size_t) noexcept { operator delete(q); }

static std::uint32_t be(const unsigned char* p, int n) { std::uint32_t v = 0; while (n--) v = v << 8 | *p++; return v; }

struct PacketView {                        // 一个 FLV tag：8 音频，9 视频，18 脚本
    int type = 0;
    std::uint32_t ts = 0;                  // 毫秒
    bool key = false;                      // 视频关键帧
    const unsigned char* data = nullptr;
    std::size_t size = 0;
};

// 读一个 tag：11 字节头 + 数据 + 4 字节 PreviousTagSize。buf 由调用方提供
static bool readTag(std::FILE* f, std::vector<unsigned char>& buf, PacketView& pkt) {
    unsigned char h[11];
    if (std::fread(h, 1, 11, f) != 11) return false;
    std::size_t n = be(h + 1, 3);
    buf.resize(n);
    if (std::fread(buf.data(), 1, n, f) != n) return false;
    unsigned char prev[4];
    if (std::fread(prev, 1, 4, f) != 4) return false;
    pkt.type = h[0];
    pkt.ts = be(h + 4, 3) | static_cast<std::uint32_t>(h[7]) << 24;
    pkt.key = pkt.type == 9 && n > 0 && (buf[0] >> 4) == 1;
    pkt.data = buf.data();
    pkt.size = n;
    return true;
}
static std::FILE* openFlv(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    unsigned char h[13];                   // 9 字节文件头 + PreviousTagSize0
    if (f && (std::fread(h, 1, 13, f) != 13 || h[0] != 'F' || h[1] != 'L' || h[2] != 'V')) { std::fclose(f); f = nullptr; }
    return f;
}

// ---- 做法一：一次全读出来 ----
struct Packet {
    int type;
    std::uint32_t ts;
    bool key;
    std::vector<unsigned char> data;
};
std::vector<Packet> readAll(const char* path) {
    std::vector<Packet> out;
    std::FILE* f = openFlv(path);
    if (!f) return out;
    std::vector<unsigned char> buf;
    PacketView v;
    while (readTag(f, buf, v)) out.push_back({v.type, v.ts, v.key, std::vector<unsigned char>(v.data, v.data + v.size)});
    std::fclose(f);
    return out;
}

// ---- 做法二：输入迭代器，++ 时才读下一个 tag ----
class FlvReader {
public:
    explicit FlvReader(const char* path) : f_(openFlv(path)) {}
    ~FlvReader() { if (f_) std::fclose(f_); }
    FlvReader(const FlvReader&) = delete;
    FlvReader& operator=(const FlvReader&) = delete;

    class iterator {
    public:
        using iterator_category = std::input_iterator_tag;   // 只能往前走一遍：++ 之后上一个包就没了
        using value_type = PacketView;
        using difference_type = std::ptrdiff_t;
        using pointer = const PacketView*;
        using reference = const PacketView&;

        iterator() = default;                                // end()：r_ 为空
        explicit iterator(FlvReader* r) : r_(r) { if (!r_->next()) r_ = nullptr; }
        reference operator*() const { return r_->cur_; }
        pointer operator->() const { return &r_->cur_; }
        iterator& operator++() { if (!r_->next()) r_ = nullptr; return *this; }
        void operator++(int) { ++*this; }
        friend bool operator==(const iterator& a, const iterator& b) { return a.r_ == b.r_; }
        friend bool operator!=(const iterator& a, const iterator& b) { return a.r_ != b.r_; }

    private:
        FlvReader* r_ = nullptr;
    };
    iterator begin() { return f_ ? iterator(this) : iterator(); }
    iterator end() { return iterator(); }
    long bytesRead() const { return f_ ? std::ftell(f_) : 0; }

private:
    bool next() { return readTag(f_, buf_, cur_); }
    std::FILE* f_;
    std::vector<unsigned char> buf_;                         // 只有一个缓冲区，每个包都复用它
    PacketView cur_;
};

using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }
template <class F> double median7(F f) {
    std::vector<double> t;
    for (int k = 0; k < 7; ++k) t.push_back(f());
    std::sort(t.begin(), t.end());
    return t[3];
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "bbb-10s.flv";
    if (FlvReader probe(path); probe.begin() == probe.end()) { std::printf("打不开 %s（用 ffmpeg -i bbb-10s.mp4 -c copy bbb-10s.flv 生成）\n", path); return 1; }

    // 1. 包数和峰值内存
    {
        g_peak = g_now; std::size_t base = g_now, c0 = g_count;
        auto all = readAll(path);
        int v = 0, a = 0, s = 0;
        for (const Packet& p : all) (p.type == 9 ? v : p.type == 8 ? a : s)++;
        std::printf("一次全读：%zu 个包（视频 %d，音频 %d，脚本 %d），峰值 %zu 字节，new %zu 次\n", all.size(), v, a, s, g_peak - base, g_count - c0);
    }
    {
        g_peak = g_now; std::size_t base = g_now, c0 = g_count;
        FlvReader r(path);
        std::size_t n = 0, bytes = 0;
        for (const PacketView& p : r) { ++n; bytes += p.size; }
        std::printf("输入迭代器：%zu 个包（数据共 %zu 字节），峰值 %zu 字节，new %zu 次\n", n, bytes, g_peak - base, g_count - c0);
    }
    // 2. 拿到第一个包要多久（前面已经把文件读过两遍，都在系统缓存里）
    double eagerFirst = median7([&] { auto t0 = Clock::now(); auto all = readAll(path); volatile int x = all.front().type; (void)x; return ms(t0, Clock::now()); });
    double lazyFirst = median7([&] { auto t0 = Clock::now(); FlvReader r(path); volatile int x = r.begin()->type; (void)x; return ms(t0, Clock::now()); });
    std::printf("拿到第一个包：一次全读 %.3f ms，输入迭代器 %.3f ms\n", eagerFirst, lazyFirst);
    // 3. 只要找到 5 秒之后的第一个关键帧（seek）就停下
    long lazyBytes = 0;
    double eagerSeek = median7([&] {
        auto t0 = Clock::now(); auto all = readAll(path);
        auto it = std::find_if(all.begin(), all.end(), [](const Packet& p) { return p.key && p.ts >= 5000; });
        volatile std::uint32_t x = it->ts; (void)x; return ms(t0, Clock::now()); });
    double lazySeek = median7([&] {
        auto t0 = Clock::now(); FlvReader r(path);
        auto it = std::find_if(r.begin(), r.end(), [](const PacketView& p) { return p.key && p.ts >= 5000; });
        volatile std::uint32_t x = it->ts; (void)x; lazyBytes = r.bytesRead(); return ms(t0, Clock::now()); });
    {
        FlvReader r(path);
        auto it = std::find_if(r.begin(), r.end(), [](const PacketView& p) { return p.key && p.ts >= 5000; });
        std::printf("找 5 秒后的第一个关键帧（ts=%u）：一次全读 %.3f ms，输入迭代器 %.3f ms，只读了 %ld 字节\n", it->ts, eagerSeek, lazySeek, lazyBytes);
    }
}
