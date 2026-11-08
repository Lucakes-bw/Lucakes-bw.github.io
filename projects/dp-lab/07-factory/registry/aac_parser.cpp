// AAC ADTS：逐帧读帧头，统计帧数、采样率、声道数（音视频专栏《AAC 码流》）
#include <cstdio>
#include "parser.h"

namespace {
const int kRates[] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350};

bool isSync(const Bytes& d, size_t p) { return p + 7 <= d.size() && d[p] == 0xFF && (d[p + 1] & 0xF6) == 0xF0; }
size_t frameLen(const Bytes& d, size_t p) { return (d[p + 3] & 0x03) << 11 | d[p + 4] << 3 | d[p + 5] >> 5; }

class AacParser : public Parser {
public:
    void parse(const Bytes& d) override {
        size_t pos = 0;
        int frames = 0;
        while (isSync(d, pos)) {
            size_t len = frameLen(d, pos);
            if (len < 7) break;
            ++frames;
            pos += len;
        }
        int rate = kRates[(d[2] >> 2) & 0x0F];
        int channels = (d[2] & 0x01) << 2 | d[3] >> 6;
        std::printf("  %d 帧，采样率 %d Hz，%d 声道，时长约 %.2f 秒\n", frames, rate, channels, frames * 1024.0 / rate);
    }
};

int probe(const Bytes& d) {                          // 同步字 0xFFF，并且按帧长跳过去，下一帧也是同步字
    if (!isSync(d, 0)) return 0;
    return isSync(d, frameLen(d, 0)) ? 90 : 30;
}

Registrar reg({"aac", probe, [] { return std::make_unique<AacParser>(); }});
}  // namespace
