// 《工厂方法》练习 3：新增 FLV 格式，只需要新加这一个文件（音视频专栏《FLV 封装格式》）
#include <cstdio>
#include "../parser.h"

namespace {
class FlvParser : public Parser {
public:
    void parse(const Bytes& d) override {
        int audio = 0, video = 0, script = 0;
        size_t pos = be32(&d[5]) + 4;                  // 跳过 FLV 头（长度记在第 5～8 字节）和第一个 PreviousTagSize
        while (pos + 11 <= d.size()) {
            int type = d[pos] & 0x1F;
            size_t size = d[pos + 1] << 16 | d[pos + 2] << 8 | d[pos + 3];
            if (type == 8) ++audio;
            else if (type == 9) ++video;
            else if (type == 18) ++script;
            pos += 11 + size + 4;                      // tag 头 11 字节 + 数据 + PreviousTagSize
        }
        std::printf("  音频 tag %d 个，视频 tag %d 个，脚本 tag %d 个\n", audio, video, script);
    }
};

int probe(const Bytes& d) {                            // 文件以 "FLV" 开头，版本号为 1
    return d.size() >= 9 && d[0] == 'F' && d[1] == 'L' && d[2] == 'V' && d[3] == 1 ? 100 : 0;
}

Registrar reg({"flv", probe, [] { return std::make_unique<FlvParser>(); }});
}  // namespace
