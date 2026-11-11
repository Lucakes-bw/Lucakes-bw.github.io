// 《迭代器模式》练习 4：找问题——遍历包队列时删掉过期的包，结果有的过期包没删掉
#include <cstdint>
#include <cstdio>
#include <vector>

struct Packet { char stream; std::int64_t dts; };

int main() {
    std::vector<Packet> queue = {{'V', 0}, {'A', 10}, {'A', 31}, {'V', 40}, {'A', 52}, {'V', 80}, {'A', 94}};
    const std::int64_t clock = 60;   // 播放时钟已经走到 60 ms：比时钟早 10 ms 以上的包来不及解码，丢掉
    for (auto it = queue.begin(); it != queue.end(); ++it)
        if (it->dts < clock - 10) queue.erase(it);
    std::printf("播放时钟 %lld ms，剩下：", static_cast<long long>(clock));
    for (const Packet& p : queue) std::printf("%c%lld ", p.stream, static_cast<long long>(p.dts));
    std::printf("\n");
}
