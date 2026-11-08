// MP4：列出顶层的 box（音视频专栏《MP4 封装格式》）
#include <cstdio>
#include "parser.h"

namespace {
class Mp4Parser : public Parser {
public:
    void parse(const Bytes& d) override {
        size_t pos = 0;
        while (pos + 8 <= d.size()) {
            uint64_t size = be32(&d[pos]);
            std::string type(d.begin() + pos + 4, d.begin() + pos + 8);
            if (size == 1 && pos + 16 <= d.size()) size = uint64_t(be32(&d[pos + 8])) << 32 | be32(&d[pos + 12]);
            if (size < 8) break;
            std::printf("  box %s  %llu 字节\n", type.c_str(), (unsigned long long)size);
            pos += size;
        }
    }
};

int probe(const Bytes& d) {                          // 第 4～7 个字节是 "ftyp"
    return d.size() >= 8 && std::string(d.begin() + 4, d.begin() + 8) == "ftyp" ? 100 : 0;
}

Registrar reg({"mp4", probe, [] { return std::make_unique<Mp4Parser>(); }});
}  // namespace
