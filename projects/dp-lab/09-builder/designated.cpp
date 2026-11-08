// 《建造者》：C++20 的指定初始化——参数少、不需要检查时，一个结构体就够了
#include <iostream>
#include <string>

struct EncodeOptions {
    std::string input;
    std::string output;
    std::string codec = "libx264";
    int crf = 23;
    std::string preset = "medium";
    int width = 0, height = 0;
    bool audio = true;
};

std::string commandLine(const EncodeOptions& o) {
    std::string cmd = "ffmpeg -i " + o.input + " -c:v " + o.codec + " -crf " + std::to_string(o.crf) + " -preset " + o.preset;
    if (o.width) cmd += " -vf scale=" + std::to_string(o.width) + ":" + std::to_string(o.height);
    return cmd + (o.audio ? " -c:a aac " : " -an ") + o.output;
}

int main() {
    // 每个值都带着名字，没写的成员用默认值
    std::cout << commandLine({.input = "bbb-10s.mp4", .output = "preview.mp4", .crf = 28, .width = 640, .height = 360}) << '\n';
    std::cout << commandLine({.input = "bbb-10s.mp4", .output = "silent.mp4", .audio = false}) << '\n';
}
