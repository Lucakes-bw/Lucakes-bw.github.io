// 《模板方法模式》：问题版加需求——三个导出函数都要支持进度回调和取消，每个函数都得改
#include <cstdio>
#include <functional>
#include <string>

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

// ---------- 三个导出函数 ----------
using Progress = std::function<bool(int percent)>;   // 返回 false 表示用户取消

bool exportMp4(const std::string& in, const std::string& out, const Progress& progress) {
    Demuxer dm;
    if (!dm.open(in)) { std::printf("打不开 %s\n", in.c_str()); return false; }
    Decoder dec(Media::Video);
    Encoder enc{"h264"};
    Writer w;
    if (!w.open(out)) { dm.close(); return false; }
    Packet pkt;
    Frame f;
    while (dm.read(pkt)) {
        if (progress && !progress(dm.percent())) {   // 取消：关文件，删掉写了一半的输出
            w.close(); w.discard(); dm.close();
            return false;
        }
        if (pkt.type != Media::Video) continue;
        if (dec.decode(pkt, f)) w.write(enc.encode(f));
    }
    while (dec.flush(f)) w.write(enc.encode(f));
    w.close();
    dm.close();
    if (progress) progress(100);
    std::printf("%-10s %4d 帧 %9ld 字节\n", out.c_str(), enc.frames, w.bytes);
    return true;
}

bool exportGif(const std::string& in, const std::string& out, const Progress& progress) {
    Demuxer dm;
    if (!dm.open(in)) { std::printf("打不开 %s\n", in.c_str()); return false; }
    Decoder dec(Media::Video);
    Encoder enc{"gif"};
    Writer w;
    if (!w.open(out)) { dm.close(); return false; }
    Packet pkt;
    Frame f;
    while (dm.read(pkt)) {
        if (progress && !progress(dm.percent())) {   // 取消：关文件，删掉写了一半的输出
            w.close(); w.discard(); dm.close();
            return false;
        }
        if (pkt.type != Media::Video) continue;
        if (!dec.decode(pkt, f)) continue;
        if (f.index % 2 != 0) continue;          // 24 fps 降到 12 fps
        f.w = 480; f.h = 270;                    // 缩小
        w.write(enc.encode(f));
    }
    while (dec.flush(f)) {
        if (f.index % 2 != 0) continue;
        f.w = 480; f.h = 270;
        w.write(enc.encode(f));
    }
    w.close();
    dm.close();
    if (progress) progress(100);
    std::printf("%-10s %4d 帧 %9ld 字节\n", out.c_str(), enc.frames, w.bytes);
    return true;
}

bool exportWav(const std::string& in, const std::string& out, const Progress& progress) {
    Demuxer dm;
    if (!dm.open(in)) { std::printf("打不开 %s\n", in.c_str()); return false; }
    Decoder dec(Media::Audio);
    Encoder enc{"pcm_s16le"};
    Writer w;
    if (!w.open(out)) { dm.close(); return false; }
    Packet pkt;
    Frame f;
    while (dm.read(pkt)) {
        if (progress && !progress(dm.percent())) {   // 取消：关文件，删掉写了一半的输出
            w.close(); w.discard(); dm.close();
            return false;
        }
        if (pkt.type != Media::Audio) continue;
        if (dec.decode(pkt, f)) w.write(enc.encode(f));
    }
    while (dec.flush(f)) w.write(enc.encode(f));
    w.close();
    dm.close();
    if (progress) progress(100);
    std::printf("%-10s %4d 帧 %9ld 字节\n", out.c_str(), enc.frames, w.bytes);
    return true;
}

int main() {
    auto show = [last = -1](int p) mutable {    // 每过 50% 打印一次
        if (p / 50 != last) std::printf("  进度 %d%%\n", p);
        last = p / 50;
        return true;
    };
    exportMp4("bbb-10s.mp4", "out.mp4", show);
    exportGif("bbb-10s.mp4", "out.gif", show);
    if (!exportWav("bbb-10s.mp4", "out.wav", [](int p) { return p < 30; }))   // 导到 30% 时取消
        std::printf("out.wav 已取消\n");
    std::printf("打开着的文件：%d\n", g_open);
}
