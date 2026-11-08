// 《建造者》：把生成的命令真的交给 ffmpeg 执行，看看结果
// 编译：clang++ -std=c++17 -O2 run_encode.cpp -o run_encode；运行：./run_encode bbb-10s.mp4
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include "encode_job.h"

std::string probeSize(const std::string& file) {     // 用 ffprobe 读出实际的宽 × 高
    std::string cmd = "ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=s=x:p=0 " + file;
    FILE* p = popen(cmd.c_str(), "r");
    char buf[64] = {0};
    if (p && std::fgets(buf, sizeof buf, p)) buf[std::strcspn(buf, "\n")] = 0;
    if (p) pclose(p);
    return buf;
}

void run(const char* label, std::string cmd, const std::string& out) {
    cmd.replace(0, 6, "ffmpeg -y -loglevel error");   // 覆盖旧文件，只显示错误
    std::filesystem::remove(out);
    std::printf("== %s\n   %s\n", label, cmd.c_str());
    std::fflush(stdout);
    auto t0 = std::chrono::steady_clock::now();
    int rc = std::system((cmd + " 2>&1 | head -1").c_str());
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    (void)rc;
    if (std::filesystem::exists(out) && std::filesystem::file_size(out) > 0)
        std::printf("   → %s，%.0f KB，用时 %.2f s\n", probeSize(out).c_str(), std::filesystem::file_size(out) / 1024.0, s);
    else
        std::printf("   → 没有生成输出文件\n");
}

int main(int argc, char** argv) {
    std::string in = argc > 1 ? argv[1] : "bbb-10s.mp4";

    auto preview = EncodeJobBuilder().input(in).output("preview_crf28.mp4").crf(28).preset("veryfast").size(640, 360).build();
    run("建造者：预览版 crf 28", preview.job->commandLine(), "preview_crf28.mp4");

    auto better = EncodeJobBuilder().input(in).output("preview_crf23.mp4").crf(23).preset("veryfast").size(640, 360).build();
    run("建造者：预览版 crf 23", better.job->commandLine(), "preview_crf23.mp4");

    // 重叠构造函数里把宽和高写反了：得到的命令照样能执行
    run("构造函数参数写反", "ffmpeg -i " + in + " -c:v libx264 -crf 28 -preset veryfast -vf scale=360:640 -c:a aac swapped.mp4", "swapped.mp4");

    // 没有检查的代码：宽度是奇数
    run("没有检查：宽度 641", "ffmpeg -i " + in + " -c:v libx264 -crf 28 -preset veryfast -vf scale=641:360 -c:a aac odd.mp4", "odd.mp4");
    auto odd = EncodeJobBuilder().input(in).output("odd.mp4").crf(28).size(641, 360).build();
    std::printf("   建造者在执行之前就拒绝了它：%s\n", odd.error.c_str());
}
