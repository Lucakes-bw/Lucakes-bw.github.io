// 《适配器》：同样的逻辑，通过适配器读取——提前返回也不会泄漏
#include <cstdio>
#include "ffmpeg_source.h"

double keyframeAfter(const char* path, double sec) {
    std::string err;
    auto src = FfmpegSource::open(path, err);
    if (!src) return -1;
    while (auto pkt = src->next())
        if (pkt->video && pkt->keyframe && pkt->time >= sec) return pkt->time;   // 直接返回，src 析构时自动关闭文件
    return -1;
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "bbb-10s.mp4";
    for (int s = 0; s < 10; ++s) std::printf("%.2f ", keyframeAfter(path, s));
    std::printf("\n");
}
