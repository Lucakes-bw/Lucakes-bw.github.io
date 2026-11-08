// H.264 Annex B 裸流：按起始码切出 NAL 单元，统计各种类型的个数（音视频专栏《H.264 码流》）
#include <cstdio>
#include <map>
#include "parser.h"

namespace {
const char* nalName(int t) {
    switch (t) {
    case 1: return "非 IDR 片";
    case 5: return "IDR 片";
    case 6: return "SEI";
    case 7: return "SPS";
    case 8: return "PPS";
    default: return "其他";
    }
}

class H264Parser : public Parser {
public:
    void parse(const Bytes& d) override {
        std::map<int, int> count;
        for (size_t i = 0; i + 3 < d.size(); ++i)
            if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1) {   // 00 00 01（00 00 00 01 的后三个字节也会匹配到）
                ++count[d[i + 3] & 0x1F];
                i += 3;
            }
        for (const auto& [type, n] : count) std::printf("  NAL 类型 %2d %-10s %5d 个\n", type, nalName(type), n);
    }
};

int probe(const Bytes& d) {
    bool start4 = d.size() >= 5 && d[0] == 0 && d[1] == 0 && d[2] == 0 && d[3] == 1;
    if (!start4) return 0;
    return (d[4] & 0x1F) == 7 ? 80 : 40;             // 以起始码开头，第一个 NAL 是 SPS：很可能是 H.264
}

Registrar reg({"h264", probe, [] { return std::make_unique<H264Parser>(); }});
}  // namespace
