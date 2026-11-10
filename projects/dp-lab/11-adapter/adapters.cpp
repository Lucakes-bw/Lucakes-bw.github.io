// 《适配器》：三种接口各不相同的数据源，适配成同一个 MediaSource
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// ---------- 目标接口：播放器只认识它 ----------
struct Packet {
    double time;
    std::vector<unsigned char> data;
};

class MediaSource {
public:
    virtual ~MediaSource() = default;
    virtual std::string describe() const = 0;
    virtual std::optional<Packet> next() = 0;
};

// 只依赖目标接口的代码
void summarize(MediaSource& src) {
    int n = 0;
    size_t bytes = 0;
    double last = 0;
    while (auto p = src.next()) { ++n; bytes += p->data.size(); last = p->time; }
    std::cout << src.describe() << "：" << n << " 个包，" << bytes << " 字节，最后一个在 " << last << " 秒\n";
}

// ---------- 被适配者一：一个旧的 C++ 类，接口风格完全不同 ----------
class LegacyWavReader {
public:
    bool openFile(const char* name) { name_ = name; pos_ = 0; return true; }
    int readChunk(unsigned char* buf, int max) {          // 返回读到的字节数，0 表示读完
        int n = pos_ + max <= kTotal ? max : kTotal - pos_;
        std::memset(buf, 0x80, n);
        pos_ += n;
        return n;
    }
    int sampleRate() const { return 48000; }
private:
    static constexpr int kTotal = 48000 * 2 * 2;          // 1 秒、48 kHz、16 位立体声
    std::string name_;
    int pos_ = 0;
};

// 类适配器：公有继承目标接口，私有继承被适配的类（「用它来实现」，不对外暴露）
class WavSource : public MediaSource, private LegacyWavReader {
public:
    explicit WavSource(const char* name) { openFile(name); }
    std::string describe() const override { return "WAV（类适配器）"; }
    std::optional<Packet> next() override {
        Packet p{time_, std::vector<unsigned char>(4096)};
        int n = readChunk(p.data.data(), 4096);          // 直接调用继承来的函数
        if (n == 0) return std::nullopt;
        p.data.resize(n);
        time_ += n / 4.0 / sampleRate();                  // 每个采样 2 声道 × 2 字节
        return p;
    }
private:
    double time_ = 0;
};

// ---------- 被适配者二：C 风格的回调 API，数据由它「推」过来 ----------
extern "C" {
typedef void (*frame_callback)(void* user, const unsigned char* data, int len, double time);
// 模拟一个采集设备：采集 frames 帧，每采到一帧就调用一次回调
int capture_run(int frames, frame_callback cb, void* user) {
    unsigned char buf[1000];
    for (int i = 0; i < frames; ++i) {
        std::memset(buf, i, sizeof buf);
        cb(user, buf, sizeof buf, i / 30.0);              // 30 fps
    }
    return 0;
}
}

// 推转拉适配器：把回调推来的数据存进队列，next() 再一个个拉出去
class CaptureSource : public MediaSource {
public:
    explicit CaptureSource(int frames) {
        // C 函数只接受普通的函数指针。用不捕获变量的 lambda（可以转换成函数指针），
        // 把 this 通过 void* user 传过去，回调里再转回来，就接回了 C++ 对象
        capture_run(frames, [](void* user, const unsigned char* d, int len, double t) {
            static_cast<CaptureSource*>(user)->queue_.push_back({t, std::vector<unsigned char>(d, d + len)});
        }, this);
    }
    std::string describe() const override { return "采集设备（推转拉适配器）"; }
    std::optional<Packet> next() override {
        if (queue_.empty()) return std::nullopt;
        Packet p = std::move(queue_.front());
        queue_.pop_front();
        return p;
    }
private:
    std::deque<Packet> queue_;
};

int main() {
    std::vector<std::unique_ptr<MediaSource>> sources;
    sources.push_back(std::make_unique<WavSource>("voice.wav"));
    sources.push_back(std::make_unique<CaptureSource>(90));
    for (auto& s : sources) summarize(*s);                // 同一个函数，处理两种完全不同的数据源
}
