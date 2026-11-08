// 《建造者》：GoF 原版——指挥者按固定的步骤指挥建造者；换一个建造者，同样的步骤得到另一种表示
#include <iostream>
#include <string>

// 建造者接口：转码任务的各个组成部分
class EncodeSpecBuilder {
public:
    virtual ~EncodeSpecBuilder() = default;
    virtual void source(const std::string& path) = 0;
    virtual void video(const std::string& codec, int crf, const std::string& preset) = 0;
    virtual void scale(int width, int height) = 0;
    virtual void audio(int kbps) = 0;                  // 0 表示去掉音频
    virtual void target(const std::string& path) = 0;
};

// 表示一：可以直接执行的 ffmpeg 命令行
class CommandLineBuilder : public EncodeSpecBuilder {
public:
    void source(const std::string& p) override { in_ = " -i " + p; }
    void video(const std::string& c, int crf, const std::string& preset) override {
        v_ = " -c:v " + c + " -crf " + std::to_string(crf) + " -preset " + preset;
    }
    void scale(int w, int h) override { v_ += " -vf scale=" + std::to_string(w) + ":" + std::to_string(h); }
    void audio(int kbps) override { a_ = kbps ? " -c:a aac -b:a " + std::to_string(kbps) + "k" : " -an"; }
    void target(const std::string& p) override { out_ = " " + p; }
    std::string result() const { return "ffmpeg" + in_ + v_ + a_ + out_; }
private:
    std::string in_, v_, a_, out_;
};

// 表示二：交给任务队列的 JSON
class JsonBuilder : public EncodeSpecBuilder {
public:
    void source(const std::string& p) override { add("input", "\"" + p + "\""); }
    void video(const std::string& c, int crf, const std::string& preset) override {
        add("codec", "\"" + c + "\""); add("crf", std::to_string(crf)); add("preset", "\"" + preset + "\"");
    }
    void scale(int w, int h) override { add("size", "[" + std::to_string(w) + ", " + std::to_string(h) + "]"); }
    void audio(int kbps) override { add("audio_kbps", std::to_string(kbps)); }
    void target(const std::string& p) override { add("output", "\"" + p + "\""); }
    std::string result() const { return "{" + body_ + "}"; }
private:
    void add(const std::string& k, const std::string& v) { body_ += (body_.empty() ? "" : ", ") + ("\"" + k + "\": " + v); }
    std::string body_;
};

// 指挥者：知道「手机预览版」「存档版」分别由哪些步骤组成，但不关心产出的是什么表示
class Director {
public:
    void mobilePreview(EncodeSpecBuilder& b, const std::string& in, const std::string& out) {
        b.source(in);
        b.video("libx264", 28, "veryfast");
        b.scale(640, 360);
        b.audio(64);
        b.target(out);
    }
    void archive(EncodeSpecBuilder& b, const std::string& in, const std::string& out) {
        b.source(in);
        b.video("libx264", 18, "slow");                // 不缩放，高画质
        b.audio(192);
        b.target(out);
    }
};

int main() {
    Director director;
    CommandLineBuilder cmd;
    JsonBuilder json;
    director.mobilePreview(cmd, "bbb-10s.mp4", "preview.mp4");
    director.mobilePreview(json, "bbb-10s.mp4", "preview.mp4");
    std::cout << cmd.result() << '\n' << json.result() << '\n';

    CommandLineBuilder cmd2;
    director.archive(cmd2, "bbb-10s.mp4", "archive.mp4");
    std::cout << cmd2.result() << '\n';
}
