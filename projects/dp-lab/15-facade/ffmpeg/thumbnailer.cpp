// 《外观模式》：Thumbnailer 外观的实现——FFmpeg 的调用顺序和释放顺序都关在这一个文件里
#include "thumbnailer.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}
#include <cstdio>
#include <stdexcept>

namespace {
// 每种 FFmpeg 对象配一个删除器，交给 unique_ptr：释放顺序由成员的声明顺序决定，提前 return 或抛异常都不会漏
struct FmtDel   { void operator()(AVFormatContext* p) const { avformat_close_input(&p); } };
struct CodecDel { void operator()(AVCodecContext* p) const { avcodec_free_context(&p); } };
struct FrameDel { void operator()(AVFrame* p) const { av_frame_free(&p); } };
struct PktDel   { void operator()(AVPacket* p) const { av_packet_free(&p); } };
struct SwsDel   { void operator()(SwsContext* p) const { sws_freeContext(p); } };
struct FileDel  { void operator()(FILE* p) const { std::fclose(p); } };
using FmtPtr   = std::unique_ptr<AVFormatContext, FmtDel>;
using CodecPtr = std::unique_ptr<AVCodecContext, CodecDel>;
using FramePtr = std::unique_ptr<AVFrame, FrameDel>;
using PktPtr   = std::unique_ptr<AVPacket, PktDel>;
using SwsPtr   = std::unique_ptr<SwsContext, SwsDel>;

void check(int ret, const char* what) {           // FFmpeg 的错误码 → 异常
    if (ret >= 0) return;
    char msg[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(ret, msg, sizeof msg);
    throw std::runtime_error(std::string(what) + "：" + msg);
}
}  // namespace

struct Thumbnailer::Impl {
    FmtPtr fmt;
    CodecPtr dec;
    int vs = -1;
    PktPtr pkt{av_packet_alloc()};
    FramePtr frame{av_frame_alloc()};
    SwsPtr sws;

    explicit Impl(const std::string& path) {
        AVFormatContext* f = nullptr;
        check(avformat_open_input(&f, path.c_str(), nullptr, nullptr), "打开文件");
        fmt.reset(f);
        check(avformat_find_stream_info(f, nullptr), "读取流信息");
        const AVCodec* codec = nullptr;
        check(vs = av_find_best_stream(f, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0), "查找视频流");
        dec.reset(avcodec_alloc_context3(codec));
        if (!dec || !pkt || !frame) throw std::bad_alloc();
        check(avcodec_parameters_to_context(dec.get(), f->streams[vs]->codecpar), "复制解码参数");
        check(avcodec_open2(dec.get(), codec, nullptr), "打开解码器");
    }

    AVStream* stream() const { return fmt->streams[vs]; }

    // seek 到 sec 之前的关键帧，再解码到 pts >= sec
    AVFrame* decodeAt(double sec) {
        int64_t target = int64_t(sec / av_q2d(stream()->time_base));
        check(av_seek_frame(fmt.get(), vs, target, AVSEEK_FLAG_BACKWARD), "seek");
        avcodec_flush_buffers(dec.get());              // 丢掉上一次 grab 留在解码器里的帧
        for (;;) {
            int ret = avcodec_receive_frame(dec.get(), frame.get());
            if (ret >= 0) {
                if (frame->best_effort_timestamp >= target) return frame.get();
                continue;                              // 下一次 receive 会先 unref
            }
            if (ret == AVERROR_EOF) throw std::runtime_error("超出了视频长度");
            if (ret != AVERROR(EAGAIN)) check(ret, "解码");
            ret = av_read_frame(fmt.get(), pkt.get());
            if (ret == AVERROR_EOF) {                  // 读完了：冲刷解码器里剩下的帧
                check(avcodec_send_packet(dec.get(), nullptr), "冲刷解码器");
                continue;
            }
            check(ret, "读包");
            ret = pkt->stream_index == vs ? avcodec_send_packet(dec.get(), pkt.get()) : 0;
            av_packet_unref(pkt.get());
            check(ret, "送包");
        }
    }
};

Thumbnailer::Thumbnailer(const std::string& video) : impl_(std::make_unique<Impl>(video)) {}
Thumbnailer::~Thumbnailer() = default;
Thumbnailer::Thumbnailer(Thumbnailer&&) noexcept = default;
Thumbnailer& Thumbnailer::operator=(Thumbnailer&&) noexcept = default;

double Thumbnailer::duration() const { return impl_->fmt->duration / double(AV_TIME_BASE); }

Image Thumbnailer::grab(double sec, int width) {
    if (width <= 0) throw std::invalid_argument("宽度必须大于 0");
    AVFrame* f = impl_->decodeAt(sec);
    Image img;
    img.width = width;
    img.height = width * f->height / f->width / 2 * 2;
    img.time = f->best_effort_timestamp * av_q2d(impl_->stream()->time_base);
    SwsContext* s = sws_getCachedContext(impl_->sws.release(), f->width, f->height, AVPixelFormat(f->format),
                                         img.width, img.height, AV_PIX_FMT_RGB24, SWS_BILINEAR,
                                         nullptr, nullptr, nullptr);
    impl_->sws.reset(s);                               // 尺寸不变时复用上一次的 SwsContext
    if (!s) throw std::runtime_error("创建 SwsContext 失败");
    img.rgb.resize(size_t(img.width) * img.height * 3);
    uint8_t* dst[4] = {img.rgb.data()};
    int stride[4] = {img.width * 3};
    sws_scale(s, f->data, f->linesize, 0, f->height, dst, stride);
    return img;
}

void Thumbnailer::save(double sec, const std::string& png, int width) { save_png(grab(sec, width), png); }

void save_png(const Image& img, const std::string& path) {
    const AVCodec* png = avcodec_find_encoder(AV_CODEC_ID_PNG);
    if (!png) throw std::runtime_error("没有 PNG 编码器");
    CodecPtr enc(avcodec_alloc_context3(png));
    FramePtr f(av_frame_alloc());
    PktPtr pkt(av_packet_alloc());
    if (!enc || !f || !pkt) throw std::bad_alloc();
    enc->width = img.width;
    enc->height = img.height;
    enc->pix_fmt = AV_PIX_FMT_RGB24;
    enc->time_base = AVRational{1, 25};
    check(avcodec_open2(enc.get(), png, nullptr), "打开 PNG 编码器");
    f->format = AV_PIX_FMT_RGB24;
    f->width = img.width;
    f->height = img.height;
    f->data[0] = const_cast<uint8_t*>(img.rgb.data());   // 借用 Image 的内存，编码器会自己复制
    f->linesize[0] = img.width * 3;
    check(avcodec_send_frame(enc.get(), f.get()), "编码");
    check(avcodec_receive_packet(enc.get(), pkt.get()), "取出 PNG");
    std::unique_ptr<FILE, FileDel> fp(std::fopen(path.c_str(), "wb"));
    if (!fp) throw std::runtime_error("无法写入 " + path);
    std::fwrite(pkt->data, 1, pkt->size, fp.get());
}

void save_thumbnail(const std::string& video, double sec, const std::string& png, int width) {
    Thumbnailer(video).save(sec, png, width);
}
