// 《适配器》：播放器只认识 MediaSource，不知道背后是 FFmpeg
#include <cstdio>
#include <map>
#include "ffmpeg_source.h"

// 只依赖 MediaSource 接口的代码：统计每路流的包数、关键帧数和字节数
void summarize(MediaSource& src) {
    std::printf("%s\n", src.describe().c_str());
    struct Stat { int packets = 0, keyframes = 0; size_t bytes = 0; };
    std::map<int, Stat> stats;
    while (auto pkt = src.next()) {
        auto& s = stats[pkt->stream];
        ++s.packets;
        s.keyframes += pkt->keyframe;
        s.bytes += pkt->data.size();
    }
    for (const auto& [i, s] : stats)
        std::printf("  流 %d：%d 个包，其中关键帧 %d 个，共 %zu KB\n", i, s.packets, s.keyframes, s.bytes / 1024);
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string err;
        auto src = FfmpegSource::open(argv[i], err);
        if (!src) { std::printf("%s\n", err.c_str()); continue; }
        summarize(*src);
    }
}
