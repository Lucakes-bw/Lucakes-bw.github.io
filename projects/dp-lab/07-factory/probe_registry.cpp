// 《工厂方法》：注册表工厂的单文件版——按文件开头的字节识别格式
// 三段字节是 registry/run.sh 从测试视频提取出的 MP4、H.264、AAC 文件的前 16 个字节
#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using Bytes = std::vector<uint8_t>;

struct Format {
    std::string name;
    std::function<int(const Bytes&)> probe;        // 返回 0～100：有多大把握是这种格式
};

std::vector<Format>& registry() {                  // Meyers 单例，保证注册之前已经构造好
    static std::vector<Format> r;
    return r;
}

struct Registrar {
    Registrar(std::string name, std::function<int(const Bytes&)> probe) { registry().push_back({std::move(name), std::move(probe)}); }
};

// 每种格式一个静态对象，main 之前完成注册；加一种格式就是再加一个这样的对象
Registrar mp4("mp4", [](const Bytes& d) { return d.size() >= 8 && d[4] == 'f' && d[5] == 't' && d[6] == 'y' && d[7] == 'p' ? 100 : 0; });
Registrar h264("h264", [](const Bytes& d) {
    if (d.size() < 5 || d[0] || d[1] || d[2] || d[3] != 1) return 0;
    return (d[4] & 0x1F) == 7 ? 80 : 40;           // 第一个 NAL 是 SPS 才有八成把握
});
Registrar aac("aac", [](const Bytes& d) { return d.size() >= 2 && d[0] == 0xFF && (d[1] & 0xF6) == 0xF0 ? 60 : 0; });

const Format* best(const Bytes& d) {
    const Format* best = nullptr;
    int bestScore = 0;
    for (const auto& f : registry()) {
        int s = f.probe(d);
        std::cout << "    " << f.name << " 打分 " << s << '\n';
        if (s > bestScore) { bestScore = s; best = &f; }
    }
    return best;
}

int main() {
    std::vector<std::pair<std::string, Bytes>> files = {
        {"bbb.mp4", {0x00, 0x00, 0x00, 0x20, 0x66, 0x74, 0x79, 0x70, 0x69, 0x73, 0x6f, 0x6d, 0x00, 0x00, 0x02, 0x00}},
        {"bbb.h264", {0x00, 0x00, 0x00, 0x01, 0x06, 0x05, 0xff, 0xff, 0xa9, 0xdc, 0x45, 0xe9, 0xbd, 0xe6, 0xd9, 0x48}},
        {"bbb.aac", {0xff, 0xf1, 0x4c, 0x80, 0x25, 0xbf, 0xfc, 0xdc, 0x00, 0x4c, 0x61, 0x76, 0x63, 0x36, 0x33, 0x2e}},
        {"readme.txt", {'H', 'e', 'l', 'l', 'o'}},
    };
    for (const auto& [name, bytes] : files) {
        std::cout << name << '\n';
        const Format* f = best(bytes);
        std::cout << "  → " << (f ? f->name : std::string("无法识别")) << '\n';
    }
}
