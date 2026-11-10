// 《装饰器模式》：给 FFmpeg 的 AVIOContext 套上「计数」装饰器——解封装器照常读文件，我们在中间数它读了几次、读了多少字节、跳了几次
extern "C" {
#include <libavformat/avformat.h>
}
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

// 一个计数装饰器：自己也是一个 AVIOContext，读写和跳转都转发给里面那个 AVIOContext
struct Counter {
    const char* name;
    int bufSize;
    AVIOContext* inner;
    long reads = 0, bytes = 0, seeks = 0;

    static int read(void* opaque, uint8_t* buf, int size) {
        auto* c = static_cast<Counter*>(opaque);
        int n = avio_read(c->inner, buf, size);
        if (n <= 0) return n == 0 ? AVERROR_EOF : n;
        ++c->reads;
        c->bytes += n;
        return n;
    }
    static int64_t seek(void* opaque, int64_t offset, int whence) {
        auto* c = static_cast<Counter*>(opaque);
        if (whence & AVSEEK_SIZE) return avio_size(c->inner);       // 问文件大小：直接转发，不算跳转
        ++c->seeks;
        return avio_seek(c->inner, offset, whence & ~AVSEEK_FORCE);
    }
};

AVIOContext* wrap(Counter& c) {                  // 返回包装好的 AVIOContext，它有自己的缓冲区
    auto* buf = static_cast<unsigned char*>(av_malloc(c.bufSize));
    return avio_alloc_context(buf, c.bufSize, 0, &c, &Counter::read, nullptr, &Counter::seek);
}

void freeWrapped(AVIOContext* pb) {
    av_freep(&pb->buffer);
    avio_context_free(&pb);
}

// 用法：./avio_count 文件 [外层缓冲字节数] [内层缓冲字节数]
int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "../../../../source/media/bbb-10s.mp4";
    int outerBuf = argc > 2 ? std::atoi(argv[2]) : 32768, innerBuf = argc > 3 ? std::atoi(argv[3]) : 4096;
    AVIOContext* file = nullptr;
    if (avio_open(&file, path, AVIO_FLAG_READ) < 0) { std::fprintf(stderr, "打不开 %s\n", path); return 1; }

    Counter inner{"内层", innerBuf, file};           // 文件 ← 内层计数 ← 外层计数 ← 解封装器
    AVIOContext* pbInner = wrap(inner);
    Counter outer{"外层", outerBuf, pbInner};
    AVIOContext* pbOuter = wrap(outer);

    AVFormatContext* fmt = avformat_alloc_context();
    fmt->pb = pbOuter;                           // 解封装器只看见最外面的 AVIOContext
    fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    if (avformat_open_input(&fmt, path, nullptr, nullptr) < 0) { std::fprintf(stderr, "解封装失败\n"); return 1; }
    avformat_find_stream_info(fmt, nullptr);

    int packets[2] = {0, 0};
    AVPacket* pkt = av_packet_alloc();
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index < 2) ++packets[pkt->stream_index];
        av_packet_unref(pkt);
    }
    std::printf("文件 %lld 字节；读出 %d 个视频包、%d 个音频包\n", (long long)avio_size(file), packets[0], packets[1]);
    for (Counter* c : {&outer, &inner})
        std::printf("  %s（缓冲 %5d 字节）：read %4ld 次，共 %ld 字节，seek %ld 次\n", c->name, c->bufSize, c->reads, c->bytes, c->seeks);

    av_packet_free(&pkt);
    avformat_close_input(&fmt);                  // 自定义 IO：AVIOContext 要自己释放，从外往里
    freeWrapped(pbOuter);
    freeWrapped(pbInner);
    avio_closep(&file);
}
