// 《迭代器模式》：迭代器失效——拿着 vector 的迭代器继续 push_back，扩容后迭代器指向已经释放的内存（用 -fsanitize=address 编译）
#include <cstdint>
#include <cstdio>
#include <vector>

struct Packet {
    int stream;             // 0 视频，1 音频
    std::int64_t dts;       // 解码时间戳（毫秒）
    int size;
};

int main() {
    std::vector<Packet> queue;              // 解复用出来、还没送进解码器的包
    queue.reserve(4);
    for (int k = 0; k < 4; ++k) queue.push_back({0, k * 40LL, 1000 + k});

    auto cur = queue.begin();               // 解码线程正在处理的包
    std::printf("cur 指向 dts=%lld，容量 %zu，数据在 %p\n", static_cast<long long>(cur->dts), queue.capacity(),
                static_cast<void*>(queue.data()));

    queue.push_back({1, 0, 300});           // 解复用又读到一个音频包：容量不够，重新分配
    std::printf("push_back 之后容量 %zu，数据在 %p\n", queue.capacity(), static_cast<void*>(queue.data()));

    std::printf("再读 cur->dts：%lld\n", static_cast<long long>(cur->dts));   // 悬空迭代器
}
