// 《代理模式》：不用代理——媒体库一打开就把 120 个视频的缩略图全部解码，可屏幕上只显示 12 个
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <vector>

// ---- 统计堆内存：当前占用 g_live（和代理模式无关，只为了量出内存）----
static std::size_t g_live = 0;
void* operator new(std::size_t n) {
    auto* p = static_cast<std::size_t*>(std::malloc(n + 16));
    if (!p) std::abort();
    *p = n;
    g_live += n;
    return reinterpret_cast<char*>(p) + 16;
}
void operator delete(void* q) noexcept {
    if (!q) return;
    auto* p = reinterpret_cast<std::size_t*>(static_cast<char*>(q) - 16);
    g_live -= *p;
    std::free(p);
}
void operator delete(void* q, std::size_t) noexcept { operator delete(q); }
// ---------------------------------------------------------------------

constexpr int kW = 160, kH = 90;          // 缩略图 160×90，RGBA，每张 57600 字节
constexpr int kPerPage = 12;              // 一屏显示 12 张（3 行 × 4 列）
static int g_decodes = 0;
static std::uint32_t g_screen = 0;        // 「屏幕」：draw 往这里写，防止被优化掉

struct Thumbnail {
    virtual ~Thumbnail() = default;
    virtual const std::string& title() const = 0;
    virtual void draw(int slot) = 0;      // 画到屏幕的第 slot 格
};

// 真实对象：构造时就解码视频的一帧（这里用一段计算模拟解码的开销）
class VideoThumb final : public Thumbnail {
public:
    explicit VideoThumb(std::string path) : path_(std::move(path)), pixels_(kW * kH * 4) {
        ++g_decodes;
        std::uint32_t h = 2166136261u;
        for (char c : path_) h = (h ^ static_cast<unsigned char>(c)) * 16777619u;
        for (int pass = 0; pass < 16; ++pass)
            for (auto& b : pixels_) { h = h * 1664525u + 1013904223u; b ^= static_cast<std::uint8_t>(h >> 24); }
    }
    const std::string& title() const override { return path_; }
    void draw(int slot) override { g_screen += pixels_[slot * 4] + static_cast<std::uint32_t>(slot); }

private:
    std::string path_;
    std::vector<std::uint8_t> pixels_;
};

class Gallery {
public:
    explicit Gallery(int n) {
        for (int i = 0; i < n; ++i)
            items_.push_back(std::make_unique<VideoThumb>("clip" + std::to_string(i) + ".mp4"));
    }
    void show_page(int page) {
        for (int slot = 0; slot < kPerPage; ++slot) {
            std::size_t i = static_cast<std::size_t>(page * kPerPage + slot);
            if (i < items_.size()) items_[i]->draw(slot);
        }
    }

private:
    std::vector<std::unique_ptr<Thumbnail>> items_;
};

int main() {
    Gallery g(120);
    std::printf("打开媒体库：解码 %3d 次，堆内存 %5zu KB\n", g_decodes, g_live / 1024);
    g.show_page(0);
    std::printf("显示第 1 屏：解码 %3d 次，堆内存 %5zu KB（屏幕上只有 %d 张）\n", g_decodes, g_live / 1024, kPerPage);
}
