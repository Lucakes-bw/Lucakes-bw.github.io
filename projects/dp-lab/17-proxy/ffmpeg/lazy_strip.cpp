// 《代理模式》：用 FFmpeg 真正解码——时间轴上 20 张缩略图，全部解码 vs 虚代理（只解码屏幕上的 4 张）
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}
#include <sys/resource.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

constexpr int kW = 320, kH = 180;     // 缩略图 320×180 RGB24
constexpr int kCount = 20;            // 时间轴：0、0.5、1……9.5 秒，共 20 张
constexpr int kVisible = 4;           // 屏幕上一次只看得到 4 张
static int g_decodes = 0;

struct Image { std::vector<std::uint8_t> rgb; };

struct Thumb {
    virtual ~Thumb() = default;
    virtual double time() const = 0;
    virtual const Image& image() = 0;
};

// 真实对象：构造时打开文件、seek、解码到目标时间、缩放成 320×180
class DecodedThumb final : public Thumb {
public:
    DecodedThumb(const std::string& path, double sec) : sec_(sec) {
        ++g_decodes;
        AVFormatContext* fmt = nullptr;
        if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) return;
        std::unique_ptr<AVFormatContext*, void (*)(AVFormatContext**)> fmt_guard(&fmt, avformat_close_input);
        const AVCodec* codec = nullptr;
        if (avformat_find_stream_info(fmt, nullptr) < 0) return;
        int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
        if (vs < 0) return;
        AVStream* st = fmt->streams[vs];
        AVCodecContext* dec = avcodec_alloc_context3(codec);
        std::unique_ptr<AVCodecContext*, void (*)(AVCodecContext**)> dec_guard(&dec, avcodec_free_context);
        avcodec_parameters_to_context(dec, st->codecpar);
        if (avcodec_open2(dec, codec, nullptr) < 0) return;
        AVPacket* pkt = av_packet_alloc();
        std::unique_ptr<AVPacket*, void (*)(AVPacket**)> pkt_guard(&pkt, av_packet_free);
        AVFrame* frame = av_frame_alloc();
        std::unique_ptr<AVFrame*, void (*)(AVFrame**)> frame_guard(&frame, av_frame_free);

        auto target = static_cast<std::int64_t>(sec / av_q2d(st->time_base));
        av_seek_frame(fmt, vs, target, AVSEEK_FLAG_BACKWARD);
        bool got = false;
        while (!got) {
            int ret = av_read_frame(fmt, pkt);
            if (ret < 0) avcodec_send_packet(dec, nullptr);
            else if (pkt->stream_index == vs) avcodec_send_packet(dec, pkt);
            av_packet_unref(pkt);
            while (!got && avcodec_receive_frame(dec, frame) >= 0) got = frame->best_effort_timestamp >= target;
            if (ret < 0 && !got) return;
        }
        SwsContext* sws = sws_getContext(frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
                                         kW, kH, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
        img_.rgb.resize(kW * kH * 3);
        std::uint8_t* dst[1] = {img_.rgb.data()};
        int stride[1] = {kW * 3};
        sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, stride);
        sws_freeContext(sws);
    }
    double time() const override { return sec_; }
    const Image& image() override { return img_; }

private:
    double sec_;
    Image img_;
};

// 虚代理：只记住路径和时间，第一次要图像时才解码
class LazyThumb final : public Thumb {
public:
    LazyThumb(std::string path, double sec) : path_(std::move(path)), sec_(sec) {}
    double time() const override { return sec_; }            // 画时间刻度不需要解码
    const Image& image() override {
        if (!real_) real_ = std::make_unique<DecodedThumb>(path_, sec_);
        return real_->image();
    }

private:
    std::string path_;
    double sec_;
    std::unique_ptr<DecodedThumb> real_;
};

using Clock = std::chrono::steady_clock;
double ms_since(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }
double median(std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; }
double peak_rss_mb() {
    rusage ru{};
    getrusage(RUSAGE_SELF, &ru);
#ifdef __APPLE__
    return ru.ru_maxrss / 1048576.0;   // macOS 单位是字节
#else
    return ru.ru_maxrss / 1024.0;      // Linux 单位是 KB
#endif
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "用法：%s 视频 eager|lazy\n", argv[0]); return 1; }
    std::string path = argv[1];
    bool lazy = std::strcmp(argv[2], "lazy") == 0;
    std::vector<double> open_ms, screen_ms, all_ms;
    std::uint32_t sum = 0;
    double screen_mb = 0;
    for (int r = 0; r < 7; ++r) {
        g_decodes = 0;
        auto t0 = Clock::now();
        std::vector<std::unique_ptr<Thumb>> strip;
        for (int i = 0; i < kCount; ++i) {
            if (lazy) strip.push_back(std::make_unique<LazyThumb>(path, i * 0.5));
            else strip.push_back(std::make_unique<DecodedThumb>(path, i * 0.5));
        }
        open_ms.push_back(ms_since(t0));
        for (int i = 0; i < kVisible; ++i) sum += strip[static_cast<std::size_t>(i)]->image().rgb.at(0);
        screen_ms.push_back(ms_since(t0));
        int first_screen = g_decodes;
        if (r == 0) screen_mb = peak_rss_mb();                // 第一次显示完第一屏时的峰值内存
        for (auto& t : strip) sum += t->image().rgb.at(0);    // 把时间轴从头拖到尾
        all_ms.push_back(ms_since(t0));
        if (r == 6) std::printf("%s：第一屏解码 %d 次，拖到底一共解码 %d 次\n", lazy ? "虚代理" : "全部解码", first_screen, g_decodes);
    }
    std::printf("打开 %.3f ms，显示第一屏 %.1f ms，拖到底 %.1f ms\n", median(open_ms), median(screen_ms), median(all_ms));
    std::printf("峰值内存：显示完第一屏时 %.1f MB，拖到底之后 %.1f MB（校验 %u）\n", screen_mb, peak_rss_mb(), sum);
}
