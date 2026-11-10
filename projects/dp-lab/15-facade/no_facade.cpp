// 《外观模式》：没有外观——每个调用方都自己按顺序调用一串子系统函数，各错各的
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

// ---------- 调用方 1：视频列表的预览图（同事 A 写的） ----------
bool preview(const std::string& in, double t, const std::string& out) {
    Demuxer* dm = demux_open(in);
    if (!dm) return false;
    Decoder* dec = dec_open(dm, demux_find_video(dm));
    if (!dec) { demux_close(dm); return false; }
    demux_seek(dm, t);
    Packet pkt; Frame f;
    demux_read(dm, &pkt);                 // 读一个包、解一帧就当成第 t 秒的画面
    dec_send(dec, pkt);
    dec_receive(dec, &f);
    Scaler* sc = scale_open(f.w, f.h, 320, 180);
    Frame small;
    scale_run(sc, f, &small);
    Encoder* enc = png_open(320, 180);
    bool ok = png_write(enc, small, out);
    png_close(enc); scale_close(sc); dec_close(dec); demux_close(dm);
    return ok;
}

// ---------- 调用方 2：分享卡片（同事 B 写的） ----------
bool share_card(const std::string& in, double t, int width, const std::string& out) {
    Demuxer* dm = demux_open(in);
    if (!dm) return false;
    Decoder* dec = dec_open(dm, demux_find_video(dm));
    if (!dec) { demux_close(dm); return false; }
    demux_seek(dm, t);                    // seek 只能落到关键帧，要一直解码到 pts >= t
    Packet pkt; Frame f; bool got = false;
    while (!got && demux_read(dm, &pkt)) {
        dec_send(dec, pkt);
        while (dec_receive(dec, &f)) if (f.pts >= t - 1e-9) { got = true; break; }
    }
    if (!got) { dec_close(dec); demux_close(dm); return false; }
    Scaler* sc = scale_open(f.w, f.h, width, width * 9 / 16);
    if (!sc) return false;                // 宽度不合法时提前返回……忘了关 dec 和 dm
    Frame small;
    scale_run(sc, f, &small);
    Encoder* enc = png_open(small.w, small.h);
    bool ok = png_write(enc, small, out);
    png_close(enc); scale_close(sc); dec_close(dec); demux_close(dm);
    return ok;
}

int main() {
    std::printf("预览图，要第 5 秒：\n");
    preview("bbb-10s.mp4", 5.0, "preview.png");
    std::printf("分享卡片，要第 5 秒，宽 640：\n");
    share_card("bbb-10s.mp4", 5.0, 640, "card.png");
    std::printf("分享卡片，宽度配置成了 0：%s\n", share_card("bbb-10s.mp4", 5.0, 0, "card.png") ? "成功" : "失败");
    std::printf("还没关闭的句柄：%d\n", g_live);
}
