// 《命令模式》：lambda 命令按引用捕获了 vector 里的片段，加一个片段后 vector 扩容，撤销写进了已经释放的内存
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

struct Clip { int id; std::string source; double volume; };
struct Timeline {
    std::vector<Clip> clips;
    Clip* find(int id) {
        for (auto& c : clips)
            if (c.id == id) return &c;
        return nullptr;
    }
};
struct Action { std::function<void()> execute, undo; };

int main() {
    Timeline tl{{{1, "bbb-10s.mp4", 1.0}, {2, "bbb-10s.mp4", 1.0}, {3, "bbb-10s.mp4", 1.0}}};
    std::vector<Action> history;

    auto setVolume = [&](int id, double v) {
        Clip& c = *tl.find(id);               // 引用的是 vector 里的元素
        double old = c.volume;
        Action a{[&c, v] { c.volume = v; }, [&c, old] { c.volume = old; }};
        a.execute();
        history.push_back(std::move(a));
    };

    setVolume(2, 0.3);
    std::printf("调音量后：#2 音量 %.1f，clips 容量 %zu\n", tl.find(2)->volume, tl.clips.capacity());
    tl.clips.push_back({4, "bbb-10s.mp4", 1.0});   // 加一个片段：容量不够，vector 搬到新内存
    std::printf("加片段后：clips 容量 %zu，旧的 3 个片段已经被释放\n", tl.clips.capacity());
    history.back().undo();                    // c 指向旧内存
    std::printf("撤销后  ：#2 音量 %.1f（应该回到 1.0）\n", tl.find(2)->volume);
}
