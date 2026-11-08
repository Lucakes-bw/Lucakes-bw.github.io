// 《建造者》：一个有 8 个参数的构造函数，调用时把宽和高写反了，编译器一声不吭
#include <iostream>
#include <string>

class EncodeJob {
public:
    EncodeJob(std::string input, std::string output, std::string codec, int crf,
              std::string preset, int width, int height, bool audio)
        : input_(std::move(input)), output_(std::move(output)), codec_(std::move(codec)), crf_(crf),
          preset_(std::move(preset)), width_(width), height_(height), audio_(audio) {}

    std::string commandLine() const {
        std::string cmd = "ffmpeg -i " + input_ + " -c:v " + codec_ + " -crf " + std::to_string(crf_) +
                          " -preset " + preset_ + " -vf scale=" + std::to_string(width_) + ":" + std::to_string(height_);
        cmd += audio_ ? " -c:a aac" : " -an";
        return cmd + " " + output_;
    }

private:
    std::string input_, output_, codec_;
    int crf_;
    std::string preset_;
    int width_, height_;
    bool audio_;
};

int main() {
    // 想要 640×360 的预览版。你能一眼看出哪个数是宽、哪个是高，true 又是什么意思吗？
    EncodeJob job("bbb-10s.mp4", "preview.mp4", "libx264", 28, "veryfast", 360, 640, true);
    std::cout << job.commandLine() << '\n';
}
