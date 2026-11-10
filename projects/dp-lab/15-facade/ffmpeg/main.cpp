// 《外观模式》：调用方只认识 Thumbnailer，不 include 任何 FFmpeg 的头文件，也不链接 FFmpeg 的库
#include "thumbnailer.h"
#include <cstdio>
#include <cstdlib>
#include <exception>

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "用法：%s 视频 秒 输出.png\n", argv[0]); return 1; }
    try {
        save_thumbnail(argv[1], std::atof(argv[2]), argv[3]);
        std::printf("写出 %s\n", argv[3]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "失败：%s\n", e.what());
        return 1;
    }
}
