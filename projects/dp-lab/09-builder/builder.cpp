// 《建造者》：用链式调用一步步设置参数，build() 时统一检查，得到一个完整、合法的转码任务
#include <iostream>
#include <optional>
#include <string>

struct EncodeJob {                                    // 产品：一旦建好就不再修改
    std::string input, output, codec, preset;
    std::optional<int> crf, bitrateK;
    int width = 0, height = 0;                        // 0 表示保持原尺寸
    bool audio = true;

    std::string commandLine() const {
        std::string cmd = "ffmpeg -i " + input + " -c:v " + codec;
        if (crf) cmd += " -crf " + std::to_string(*crf);
        if (bitrateK) cmd += " -b:v " + std::to_string(*bitrateK) + "k";
        cmd += " -preset " + preset;
        if (width) cmd += " -vf scale=" + std::to_string(width) + ":" + std::to_string(height);
        cmd += audio ? " -c:a aac" : " -an";
        return cmd + " " + output;
    }
};

struct BuildResult {                                  // 要么得到任务，要么得到错误原因
    std::optional<EncodeJob> job;
    std::string error;
};

class EncodeJobBuilder {
public:
    // 每个设置函数都返回 *this 的引用，所以可以一路点下去
    EncodeJobBuilder& input(std::string path) { job_.input = std::move(path); return *this; }
    EncodeJobBuilder& output(std::string path) { job_.output = std::move(path); return *this; }
    EncodeJobBuilder& codec(std::string c) { job_.codec = std::move(c); return *this; }
    EncodeJobBuilder& preset(std::string p) { job_.preset = std::move(p); return *this; }
    EncodeJobBuilder& crf(int q) { job_.crf = q; return *this; }
    EncodeJobBuilder& bitrate(int kbps) { job_.bitrateK = kbps; return *this; }
    EncodeJobBuilder& size(int width, int height) { job_.width = width; job_.height = height; return *this; }
    EncodeJobBuilder& noAudio() { job_.audio = false; return *this; }

    // 所有检查集中在这里：要么返回一个合法的任务，要么说明哪里不对
    BuildResult build() const {
        if (job_.input.empty()) return {std::nullopt, "缺少输入文件"};
        if (job_.output.empty()) return {std::nullopt, "缺少输出文件"};
        if (job_.crf && job_.bitrateK) return {std::nullopt, "crf 和码率只能二选一"};
        if (job_.crf && (*job_.crf < 0 || *job_.crf > 51)) return {std::nullopt, "crf 必须在 0～51 之间"};
        if (job_.width % 2 || job_.height % 2) return {std::nullopt, "libx264 要求宽和高都是偶数"};
        return {job_, ""};
    }

private:
    EncodeJob job_{"", "", "libx264", "medium", std::nullopt, std::nullopt, 0, 0, true};   // 默认值集中在一处
};

void show(const BuildResult& r) {
    if (r.job) std::cout << "✓ " << r.job->commandLine() << '\n';
    else std::cout << "✗ " << r.error << '\n';
}

int main() {
    show(EncodeJobBuilder().input("bbb-10s.mp4").output("preview.mp4")
             .crf(28).preset("veryfast").size(640, 360).build());           // 每个参数都带着名字
    show(EncodeJobBuilder().input("bbb-10s.mp4").output("archive.mp4").build());   // 其余用默认值
    show(EncodeJobBuilder().input("bbb-10s.mp4").crf(28).build());          // 忘了输出
    show(EncodeJobBuilder().input("bbb-10s.mp4").output("x.mp4").crf(28).bitrate(800).build());
    show(EncodeJobBuilder().input("bbb-10s.mp4").output("x.mp4").size(641, 360).build());
}
