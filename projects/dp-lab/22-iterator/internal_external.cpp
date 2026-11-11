// 《迭代器模式》：内部迭代器（forEach + 回调）和外部迭代器（begin/end）——按 dts 交错合并音频、视频两个包队列
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <vector>

struct Packet { char stream; std::int64_t dts; };   // 'V' 视频，'A' 音频；dts 毫秒

class PacketQueue {
public:
    void push(Packet p) { q_.push_back(p); }
    // 内部迭代器：容器自己控制循环，每个元素调用一次 f；f 返回 false 就提前停下
    template <class F>
    void forEach(F f) const {
        for (const Packet& p : q_)
            if (!f(p)) return;
    }
    // 外部迭代器：把「走到哪了」交给调用方
    auto begin() const { return q_.begin(); }
    auto end() const { return q_.end(); }

private:
    std::vector<Packet> q_;
};

int main() {
    PacketQueue video, audio;
    for (int k = 0; k < 4; ++k) video.push({'V', k * 40LL});   // 25 fps
    for (int k = 0; k < 7; ++k) audio.push({'A', k * 21LL});   // 48 kHz、1024 采样一帧

    // 内部迭代器：一个一个地处理很方便，提前停也行
    int n = 0;
    video.forEach([&](const Packet& p) { ++n; return p.dts < 80; });
    std::printf("forEach 处理到 dts >= 80 停下：看了 %d 个包\n", n);

    // 合并两个队列：要同时在两个序列上各走各的，forEach 做不到（它只能一口气走完一个）
    // 外部迭代器：两个迭代器交替前进，std::merge 现成可用
    std::vector<Packet> out;
    std::merge(video.begin(), video.end(), audio.begin(), audio.end(), std::back_inserter(out),
               [](const Packet& a, const Packet& b) { return a.dts < b.dts; });
    std::printf("按 dts 交错：");
    for (const Packet& p : out) std::printf("%c%lld ", p.stream, static_cast<long long>(p.dts));
    std::printf("\n");

    // 只有 forEach 的话：先把一个队列复制出来，再在 forEach 里手工推进另一个
    std::vector<Packet> a2;
    audio.forEach([&](const Packet& p) { a2.push_back(p); return true; });
    std::size_t i = 0;
    std::printf("只用 forEach：");
    video.forEach([&](const Packet& v) {
        for (; i < a2.size() && a2[i].dts < v.dts; ++i) std::printf("%c%lld ", a2[i].stream, static_cast<long long>(a2[i].dts));
        std::printf("%c%lld ", v.stream, static_cast<long long>(v.dts));
        return true;
    });
    for (; i < a2.size(); ++i) std::printf("%c%lld ", a2[i].stream, static_cast<long long>(a2[i].dts));
    std::printf("\n");
}
