// 《外观模式》：外观的额外开销——直接调 C API、一次性的 save_thumbnail()、复用同一个 Thumbnailer，各取 10 张缩略图（请用 -O2 编译）
#include "thumbnailer.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

int save_thumbnail_raw(const char* in, double sec, int width, const char* out);   // raw_thumb.cpp

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "用法：%s 视频 输出目录\n", argv[0]); return 1; }
    const std::string video = argv[1], dir = argv[2];
    const double ts[] = {0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5, 9.5};
    auto out = [&](const char* tag, int i) { return dir + "/" + tag + std::to_string(i) + ".png"; };

    auto raw = [&] { int i = 0; for (double t : ts) save_thumbnail_raw(video.c_str(), t, 320, out("raw", i++).c_str()); };
    auto once = [&] { int i = 0; for (double t : ts) save_thumbnail(video, t, out("once", i++)); };
    auto reuse = [&] { Thumbnailer th(video); int i = 0; for (double t : ts) th.save(t, out("reuse", i++)); };

    const char* names[] = {"直接调 C API", "save_thumbnail()", "复用 Thumbnailer"};
    std::vector<double> ms[3];
    for (int round = 0; round < 7; ++round)
        for (int k = 0; k < 3; ++k) {
            int v = (k + round) % 3;                   // 每轮换一下顺序
            auto t0 = std::chrono::steady_clock::now();
            if (v == 0) raw(); else if (v == 1) once(); else reuse();
            ms[v].push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 10);
        }
    for (int v = 0; v < 3; ++v) {
        std::sort(ms[v].begin(), ms[v].end());
        std::printf("%-20s 每张 %.2f ms（7 次的中位数）\n", names[v], ms[v][3]);
    }
}
