// 《外观模式》：外观不禁止直接使用子系统——一次性函数、可复用的对象、绕过外观三种用法的代价
#include <cstdio>
#include <memory>
#include <optional>
#include <string>

// ---------- 子系统：仿照 FFmpeg 的 C 接口写的简化版 ----------
int g_live = 0, g_opened = 0, g_decoded = 0;          // 没关闭的句柄、打开过几次文件、解码了几个包
const int kKey[] = {0, 31, 79, 83, 131, 179, 227};    // bbb-10s.mp4 的关键帧（第几帧，24 帧/秒）
struct Packet { int n = 0; double pts = 0; bool key = false; };
struct Frame  { double pts = -1; int w = 1280, h = 720; };
struct Demuxer { int next = 0; };
struct Decoder { Packet in; bool has = false; };
struct Scaler  { int w, h; };
struct Encoder { int w, h; };

Demuxer* demux_open(const std::string& path) { if (path.empty()) return nullptr; ++g_live; ++g_opened; return new Demuxer; }
int  demux_find_video(Demuxer*) { return 0; }
void demux_seek(Demuxer* d, double t) {               // 只能跳到 t 之前最近的关键帧
    for (int k : kKey) if (k / 24.0 <= t + 1e-9) d->next = k;
}
bool demux_read(Demuxer* d, Packet* p) {
    if (d->next >= 240) return false;
    p->n = d->next++; p->pts = p->n / 24.0; p->key = false;
    for (int k : kKey) if (k == p->n) p->key = true;
    return true;
}
void demux_close(Demuxer* d) { if (d) { --g_live; delete d; } }

Decoder* dec_open(Demuxer*, int stream) { if (stream < 0) return nullptr; ++g_live; return new Decoder; }
void dec_send(Decoder* c, const Packet& p) { c->in = p; c->has = true; ++g_decoded; }
bool dec_receive(Decoder* c, Frame* f) { if (!c->has) return false; c->has = false; f->pts = c->in.pts; return true; }
void dec_close(Decoder* c) { if (c) { --g_live; delete c; } }

Scaler* scale_open(int, int, int w, int h) { if (w <= 0 || h <= 0) return nullptr; ++g_live; return new Scaler{w, h}; }
void scale_run(Scaler* s, const Frame& in, Frame* out) { *out = in; out->w = s->w; out->h = s->h; }
void scale_close(Scaler* s) { if (s) { --g_live; delete s; } }

Encoder* png_open(int w, int h) { ++g_live; return new Encoder{w, h}; }
bool png_write(Encoder*, const Frame& f, const std::string& path) {
    std::printf("  写出 %s：%dx%d，第 %.3f 秒的画面\n", path.c_str(), f.w, f.h, f.pts);
    return true;
}
void png_close(Encoder* e) { if (e) { --g_live; delete e; } }

// ---------- 外观：把上面那串调用和释放顺序收进一个类 ----------
class Thumbnailer {
public:
    explicit Thumbnailer(const std::string& path) : dm_(demux_open(path), demux_close) {
        if (dm_) dec_.reset(dec_open(dm_.get(), demux_find_video(dm_.get())));
    }
    bool ok() const { return dm_ && dec_; }

    // 第 t 秒的画面：seek 只能落到前面的关键帧，所以要一直解码到 pts >= t
    std::optional<Frame> grab(double t) {
        if (!ok()) return std::nullopt;
        demux_seek(dm_.get(), t);
        Packet pkt; Frame f;
        while (demux_read(dm_.get(), &pkt)) {
            dec_send(dec_.get(), pkt);
            while (dec_receive(dec_.get(), &f)) if (f.pts >= t - 1e-9) return f;
        }
        return std::nullopt;
    }

    // 第 t 秒的画面缩放到宽 width，存成 PNG
    bool save(double t, const std::string& out, int width = 320) {
        auto f = grab(t);
        if (!f || width <= 0) return false;
        Handle<Scaler> sc(scale_open(f->w, f->h, width, width * f->h / f->w), scale_close);
        if (!sc) return false;            // 提前返回也没关系：所有句柄都由析构函数关闭
        Frame small;
        scale_run(sc.get(), *f, &small);
        Handle<Encoder> enc(png_open(small.w, small.h), png_close);
        return png_write(enc.get(), small, out);
    }

private:
    template <class T> using Handle = std::unique_ptr<T, void (*)(T*)>;
    Handle<Demuxer> dm_;
    Handle<Decoder> dec_{nullptr, dec_close};   // 后声明的先析构：先关解码器，再关文件
};

// 只要一张图时，一个函数就够了
bool save_thumbnail(const std::string& in, double t, const std::string& out, int width = 320) {
    return Thumbnailer(in).save(t, out, width);
}

int main() {
    const double ts[] = {0.5, 1.5, 2.5, 3.5, 4.5, 5.5, 6.5, 7.5, 8.5, 9.5};
    std::printf("10 张缩略图，每次新建一个 Thumbnailer（和 save_thumbnail() 一样）：\n");
    for (double t : ts) Thumbnailer("bbb-10s.mp4").grab(t);
    std::printf("  打开文件 %d 次，解码 %d 个包\n", g_opened, g_decoded);

    g_opened = g_decoded = 0;
    std::printf("10 张缩略图，用同一个 Thumbnailer 对象：\n");
    Thumbnailer th("bbb-10s.mp4");
    for (double t : ts) th.grab(t);
    std::printf("  打开文件 %d 次，解码 %d 个包\n", g_opened, g_decoded);

    // 外观没提供的功能，直接用子系统：列出所有关键帧（拖动进度条时吸附到关键帧），不需要解码
    g_opened = g_decoded = 0;
    std::printf("绕过外观，直接用 Demuxer 列出关键帧：\n ");
    Demuxer* dm = demux_open("bbb-10s.mp4");
    Packet pkt;
    while (demux_read(dm, &pkt)) if (pkt.key) std::printf(" %.3f", pkt.pts);
    demux_close(dm);
    std::printf("\n  打开文件 %d 次，解码 %d 个包\n", g_opened, g_decoded);
}
