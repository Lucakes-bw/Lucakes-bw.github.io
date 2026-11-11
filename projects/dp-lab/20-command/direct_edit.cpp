// 《命令模式》：直接改状态的剪辑器，想撤销只能每次存一份整条时间线的快照，内存随操作数线性增长
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

// ---- 统计堆上还活着的字节数（替换全局 operator new / delete）----
static std::size_t g_live = 0;
void* operator new(std::size_t n) {
    auto* p = static_cast<std::size_t*>(std::malloc(n + 16));
    if (!p) std::abort();
    *p = n;
    g_live += n;
    return reinterpret_cast<char*>(p) + 16;
}
void operator delete(void* p) noexcept {
    if (!p) return;
    auto* h = reinterpret_cast<std::size_t*>(static_cast<char*>(p) - 16);
    g_live -= *h;
    std::free(h);
}
void operator delete(void* p, std::size_t) noexcept { operator delete(p); }

struct Clip {
    int id;
    std::string source;              // 素材文件
    int start, in, out;              // 时间线上的起点；素材里的入点、出点（毫秒）
    double volume = 1.0;
    std::vector<std::string> filters;
};

struct Timeline {
    std::vector<Clip> clips;
    Clip* find(int id) {
        for (auto& c : clips)
            if (c.id == id) return &c;
        return nullptr;
    }
};

// 剪辑器：每个操作直接改时间线。它不知道「改之前是什么样」，
// 想撤销，只能在每次改之前把整条时间线复制一份
class Editor {
public:
    explicit Editor(Timeline& tl) : tl_(tl) {}
    void move(int id, int start)          { save(); tl_.find(id)->start = start; }
    void setVolume(int id, double v)      { save(); tl_.find(id)->volume = v; }
    void addFilter(int id, std::string f) { save(); tl_.find(id)->filters.push_back(std::move(f)); }
    bool undo() {
        if (history_.empty()) return false;
        tl_ = std::move(history_.back());
        history_.pop_back();
        return true;
    }
    std::size_t steps() const { return history_.size(); }

private:
    void save() { history_.push_back(tl_); }   // 整条时间线的快照
    Timeline& tl_;
    std::vector<Timeline> history_;
};

int main() {
    Timeline tl;
    for (int i = 0; i < 200; ++i)    // 200 个片段，都剪自 bbb-10s.mp4，每个带一个缩放滤镜
        tl.clips.push_back({i + 1, "bbb-10s.mp4", i * 2000, i * 40 % 8000, i * 40 % 8000 + 2000, 1.0,
                            {"scale=1920:1080:flags=lanczos"}});
    std::size_t base = g_live;
    std::printf("时间线本身：%zu 个片段，堆上 %.1f KB\n", tl.clips.size(), base / 1024.0);

    Editor ed(tl);
    int done = 0;
    for (int target : {1, 10, 100, 1000}) {
        for (; done < target; ++done) {
            int id = done * 37 % 200 + 1;
            if (done % 2 == 0) ed.move(id, tl.find(id)->start + 10);
            else ed.setVolume(id, 0.1 * (done % 7 + 1));
        }
        std::printf("操作 %4d 次：撤销历史 %4zu 份快照，多占 %8.1f KB\n", done, ed.steps(),
                    (g_live - base) / 1024.0);
    }
    int id = 999 * 37 % 200 + 1;
    std::printf("最后一步把 #%d 的音量改成了 %.1f\n", id, tl.find(id)->volume);
    ed.undo();
    std::printf("撤销一步：#%d 的音量回到 %.1f\n", id, tl.find(id)->volume);
}
