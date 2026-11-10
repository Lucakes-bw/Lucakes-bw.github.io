// 《外观模式》：用同一个 Thumbnailer 取 9 张图，拼成 3×3 的缩略图墙，并打印每张图要的时间和实际拿到的时间
#include "thumbnailer.h"
#include <cstdio>
#include <cstring>
#include <exception>

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "用法：%s 视频 输出.png\n", argv[0]); return 1; }
    try {
        Thumbnailer th(argv[1]);
        const int cols = 3, rows = 3, w = 320, gap = 4;
        Image sheet;
        for (int i = 0; i < cols * rows; ++i) {
            double want = th.duration() * (i + 0.5) / (cols * rows);
            Image img = th.grab(want, w);
            std::printf("要第 %.3f 秒，拿到第 %.3f 秒\n", want, img.time);
            if (sheet.rgb.empty()) {                       // 第一张图决定格子大小
                sheet.width = cols * w + (cols + 1) * gap;
                sheet.height = rows * img.height + (rows + 1) * gap;
                sheet.rgb.assign(size_t(sheet.width) * sheet.height * 3, 24);
            }
            int x0 = gap + (i % cols) * (w + gap), y0 = gap + (i / cols) * (img.height + gap);
            for (int y = 0; y < img.height; ++y)
                std::memcpy(&sheet.rgb[(size_t(y0 + y) * sheet.width + x0) * 3], &img.rgb[size_t(y) * w * 3], w * 3);
        }
        save_png(sheet, argv[2]);
        std::printf("写出 %s：%dx%d\n", argv[2], sheet.width, sheet.height);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "失败：%s\n", e.what());
        return 1;
    }
}
