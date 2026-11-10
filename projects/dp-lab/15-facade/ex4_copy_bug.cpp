// 《外观模式》：练习 4——外观用裸指针保存句柄，按值传参后同一个句柄被关闭两次（请用 -fsanitize=address 运行）
#include <cstdio>
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

// ---------- 练习 4 的外观：用裸指针保存子系统的句柄 ----------
class Thumbnailer {
public:
    explicit Thumbnailer(const std::string& path) : dm_(demux_open(path)) {
        if (dm_) dec_ = dec_open(dm_, demux_find_video(dm_));
    }
    ~Thumbnailer() { dec_close(dec_); demux_close(dm_); }
    bool ok() const { return dm_ && dec_; }
private:
    Demuxer* dm_ = nullptr;
    Decoder* dec_ = nullptr;
};

void make_cards(Thumbnailer th) {          // 按值传参
    std::printf("  生成卡片：%s\n", th.ok() ? "可以" : "不行");
}

int main() {
    Thumbnailer th("bbb-10s.mp4");
    std::printf("打开后，还没关闭的句柄：%d\n", g_live);
    make_cards(th);
    std::printf("make_cards() 返回后，还没关闭的句柄：%d\n", g_live);
}
