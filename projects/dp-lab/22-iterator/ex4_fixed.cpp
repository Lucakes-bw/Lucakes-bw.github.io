// 《迭代器模式》练习 4 答案：用 erase 的返回值继续走，或者干脆用 erase + remove_if（C++20 是 std::erase_if）
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

struct Packet { char stream; std::int64_t dts; };

int main() {
    std::vector<Packet> queue = {{'V', 0}, {'A', 10}, {'A', 31}, {'V', 40}, {'A', 52}, {'V', 80}, {'A', 94}};
    const std::int64_t clock = 60;   // 播放时钟已经走到 60 ms：比时钟早 10 ms 以上的包来不及解码，丢掉
    std::vector<Packet> q2 = queue;
    // 改法一：erase 返回被删元素后面那个元素的迭代器；删了就不再 ++
    for (auto it = queue.begin(); it != queue.end();) {
        if (it->dts < clock - 10) it = queue.erase(it);
        else ++it;
    }
    // 改法二：erase-remove 惯用法，一遍搬完，O(n)；上面的写法每删一个都要把后面的元素往前挪，最坏 O(n²)
    q2.erase(std::remove_if(q2.begin(), q2.end(), [&](const Packet& p) { return p.dts < clock - 10; }), q2.end());
    std::printf("erase-remove 剩下 %zu 个包\n", q2.size());
    std::printf("播放时钟 %lld ms，剩下：", static_cast<long long>(clock));
    for (const Packet& p : queue) std::printf("%c%lld ", p.stream, static_cast<long long>(p.dts));
    std::printf("\n");
}
