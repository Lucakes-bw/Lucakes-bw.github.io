// 《模板方法模式》：练习 2——加「导出封面」：只要第一帧，基类加一个钩子 done()，拿到就停止读包
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

// ---------- 模拟的媒体库（代替 FFmpeg，只计数、不做真正的编解码） ----------
enum class Media { Video, Audio };
struct Packet { Media type; int index; };
struct Frame { Media type; int index; int w, h; };

int g_open = 0;                                  // 当前打开着的文件个数

struct Demuxer {                                 // 像 bbb-10s.mp4：240 个视频包、470 个音频包，交错存放
    int v = 0, a = 0;
    bool open(const std::string& path) { ++g_open; return !path.empty(); }
    bool read(Packet& p) {
        if (v == 240 && a == 470) return false;
        bool video = a == 470 || (v < 240 && v * 470 <= a * 240);
        p = video ? Packet{Media::Video, v++} : Packet{Media::Audio, a++};
        return true;
    }
    int percent() const { return (v + a) * 100 / 710; }
    void close() { --g_open; }
};

struct Decoder {                                 // 视频解码器有 2 帧延迟（B 帧重排），最后要冲刷出来
    Media type;
    int pending = 0, next = 0;
    explicit Decoder(Media t) : type(t) {}
    bool decode(const Packet&, Frame& f) {
        ++pending;
        return flushOne(type == Media::Video ? 2 : 0, f);
    }
    bool flush(Frame& f) { return flushOne(0, f); }
    bool flushOne(int keep, Frame& f) {
        if (pending <= keep) return false;
        --pending;
        f = {type, next++, 1280, 720};
        return true;
    }
};

struct Encoder {                                 // 按编码器估算每帧的字节数
    std::string codec;
    int frames = 0;
    int encode(const Frame& f) {
        ++frames;
        if (codec == "gif") return f.w * f.h / 3;
        if (codec == "pcm_s16le") return 1024 * 2 * 2;
        if (codec == "mjpeg") return f.w * f.h / 8;
        return f.w * f.h / 40;                   // h264
    }
};

struct Writer {
    std::string path;
    long bytes = 0;
    bool open(const std::string& p) { path = p; ++g_open; return !p.empty(); }
    void write(int n) { bytes += n; }
    void close() { --g_open; }
    void discard() { bytes = 0; }                 // 删掉写了一半的文件
};

// ---------- 模板方法 ----------
class ExportJob {
public:
    virtual ~ExportJob() = default;
    bool run(const std::string& in, const std::string& out) {   // 模板方法：公有、非虚，流程只写这一遍
        Demuxer dm;
        if (!dm.open(in)) { std::printf("打不开 %s\n", in.c_str()); return false; }
        Decoder dec(media());
        Encoder enc{codec()};
        Writer w;
        if (!w.open(out)) { dm.close(); return false; }
        Packet pkt;
        Frame f;
        while (!done(enc.frames) && dm.read(pkt)) {   // 新增：问一下子类够了没有
            if (pkt.type != media()) continue;
            if (dec.decode(pkt, f)) encodeOne(f, enc, w);
        }
        while (!done(enc.frames) && dec.flush(f)) encodeOne(f, enc, w);
        w.close();
        dm.close();
        std::printf("读了 %3d 个包  ", dm.v + dm.a);
        onFinished(out, enc.frames, w.bytes);
        return true;
    }

protected:
    virtual Media media() const = 0;                     // 基本操作：子类必须实现
    virtual const char* codec() const = 0;
    virtual bool process(Frame&) { return true; }        // 钩子：默认什么都不做；返回 false 丢掉这一帧
    virtual bool done(int /*framesEncoded*/) const { return false; }   // 新钩子：默认一直读到文件末尾
    virtual void onFinished(const std::string& out, int frames, long bytes) {   // 钩子：默认打印一行
        std::printf("%-10s %4d 帧 %9ld 字节\n", out.c_str(), frames, bytes);
    }

private:
    void encodeOne(Frame& f, Encoder& enc, Writer& w) {
        if (process(f)) w.write(enc.encode(f));
    }
};

class Mp4Export : public ExportJob {
protected:
    Media media() const override { return Media::Video; }
    const char* codec() const override { return "h264"; }
};

class GifExport : public ExportJob {
protected:
    Media media() const override { return Media::Video; }
    const char* codec() const override { return "gif"; }
    bool process(Frame& f) override {
        if (f.index % 2 != 0) return false;              // 24 fps 降到 12 fps
        f.w = 480; f.h = 270;                            // 缩小
        return true;
    }
};

class WavExport : public ExportJob {
protected:
    Media media() const override { return Media::Audio; }
    const char* codec() const override { return "pcm_s16le"; }
};

class CoverExport : public ExportJob {             // 新子类：第一帧存成 JPEG
protected:
    Media media() const override { return Media::Video; }
    const char* codec() const override { return "mjpeg"; }
    bool done(int framesEncoded) const override { return framesEncoded >= 1; }
};

int main() {
    std::vector<std::pair<std::unique_ptr<ExportJob>, std::string>> jobs;
    jobs.emplace_back(std::make_unique<Mp4Export>(), "out.mp4");
    jobs.emplace_back(std::make_unique<GifExport>(), "out.gif");
    jobs.emplace_back(std::make_unique<WavExport>(), "out.wav");
    jobs.emplace_back(std::make_unique<CoverExport>(), "cover.jpg");
    for (auto& [job, out] : jobs) job->run("bbb-10s.mp4", out);
    std::printf("打开着的文件：%d\n", g_open);
}
