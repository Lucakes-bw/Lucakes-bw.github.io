// 《代理模式》：练习 4——找问题：auto 返回类型推导出 vector<bool>::reference，指向已经销毁的局部 vector
#include <cstdio>
#include <vector>

// 扫描一遍，记下每个包是不是关键帧（这里用每 48 个包一个关键帧模拟）
std::vector<bool> scan_keyframes(int packets) {
    std::vector<bool> key(static_cast<std::size_t>(packets));
    for (int i = 0; i < packets; i += 48) key[static_cast<std::size_t>(i)] = true;
    return key;
}

auto is_keyframe(int packets, int i) {
    std::vector<bool> key = scan_keyframes(packets);
    return key[static_cast<std::size_t>(i)];       // 返回的是代理对象，指向 key 里的一个字
}                                                  // key 在这里析构

int main() {
    int n = 0;
    for (int i = 0; i < 240; ++i) n += is_keyframe(240, i);   // 应该是 5
    std::printf("关键帧 %d 个\n", n);
}
