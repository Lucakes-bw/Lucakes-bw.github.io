// 《责任链模式》：解码器回退链——VideoToolbox → VAAPI → CUDA → 软解，第一个能打开解码器的处理者负责解码
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
}
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/resource.h>

using Clock = std::chrono::steady_clock;
static double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }
static double cpuMs() {             // 本进程所有线程用掉的 CPU 时间（用户态 + 内核态）
  rusage u; getrusage(RUSAGE_SELF, &u);
  return (u.ru_utime.tv_sec + u.ru_stime.tv_sec) * 1e3 + (u.ru_utime.tv_usec + u.ru_stime.tv_usec) / 1e3;
}
static std::string errStr(int err) { char b[128]; av_strerror(err, b, sizeof b); return b; }

class DecoderHandler {                     // 处理者：试着打开解码器，打不开就交给下一个
 public:
  virtual ~DecoderHandler() = default;
  DecoderHandler* setNext(DecoderHandler* next) { next_ = next; return next; }
  AVCodecContext* open(const AVCodecParameters* par) {
    auto t = Clock::now();
    std::string why;
    AVCodecContext* ctx = tryOpen(par, why);
    std::printf("  %-12s %s（%.2f ms）\n", name(), ctx ? "✓ 打开成功" : ("✗ " + why).c_str(), msSince(t));
    if (ctx) return ctx;
    return next_ ? next_->open(par) : nullptr;   // 这一环打不开：交给下一环
  }
  virtual bool hwFrame(const AVFrame*) const { return false; }
 protected:
  virtual const char* name() const = 0;
  virtual AVCodecContext* tryOpen(const AVCodecParameters* par, std::string& why) = 0;
 private:
  DecoderHandler* next_ = nullptr;
};

class HwDecoder : public DecoderHandler {  // 硬解：设备类型不同，代码相同
 public:
  explicit HwDecoder(AVHWDeviceType t) : type_(t) {}
  bool hwFrame(const AVFrame* f) const override { return f->format == hwFmt_; }
 protected:
  const char* name() const override { return av_hwdevice_get_type_name(type_); }
  AVCodecContext* tryOpen(const AVCodecParameters* par, std::string& why) override {
    const AVCodec* dec = avcodec_find_decoder(par->codec_id);
    for (int i = 0;; ++i) {                // 1. 这个解码器支不支持这种硬件
      const AVCodecHWConfig* cfg = avcodec_get_hw_config(dec, i);
      if (!cfg) { why = "解码器不支持这种硬件"; return nullptr; }
      if ((cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) && cfg->device_type == type_) { hwFmt_ = cfg->pix_fmt; break; }
    }
    AVBufferRef* dev = nullptr;            // 2. 能不能创建硬件设备
    if (int err = av_hwdevice_ctx_create(&dev, type_, nullptr, nullptr, 0); err < 0) { why = "创建设备失败：" + errStr(err); return nullptr; }
    AVCodecContext* ctx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(ctx, par);
    ctx->hw_device_ctx = dev;
    ctx->opaque = this;
    ctx->get_format = pickFormat;
    if (int err = avcodec_open2(ctx, dec, nullptr); err < 0) { why = errStr(err); avcodec_free_context(&ctx); return nullptr; }
    return ctx;
  }
 private:
  static AVPixelFormat pickFormat(AVCodecContext* c, const AVPixelFormat* fmts) {
    auto self = static_cast<HwDecoder*>(c->opaque);
    for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p)
      if (*p == self->hwFmt_) return *p;
    return avcodec_default_get_format(c, fmts);   // FFmpeg 内部也有一条回退：硬件格式不可用就用软件格式
  }
  AVHWDeviceType type_;
  AVPixelFormat hwFmt_ = AV_PIX_FMT_NONE;
};

class SwDecoder : public DecoderHandler {  // 软解：链尾，几乎总能打开
 protected:
  const char* name() const override { return "software"; }
  AVCodecContext* tryOpen(const AVCodecParameters* par, std::string& why) override {
    const AVCodec* dec = avcodec_find_decoder(par->codec_id);
    if (!dec) { why = "没有这种解码器"; return nullptr; }
    AVCodecContext* ctx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(ctx, par);
    if (int err = avcodec_open2(ctx, dec, nullptr); err < 0) { why = errStr(err); avcodec_free_context(&ctx); return nullptr; }
    return ctx;
  }
};

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "用法：%s 视频文件 [sw]\n", argv[0]); return 1; }
  bool swOnly = argc > 2 && std::strcmp(argv[2], "sw") == 0;
  av_log_set_level(AV_LOG_QUIET);
  AVFormatContext* fmt = nullptr;
  if (avformat_open_input(&fmt, argv[1], nullptr, nullptr) < 0 || avformat_find_stream_info(fmt, nullptr) < 0) return 1;
  int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);

  HwDecoder vt(AV_HWDEVICE_TYPE_VIDEOTOOLBOX), vaapi(AV_HWDEVICE_TYPE_VAAPI), cuda(AV_HWDEVICE_TYPE_CUDA);
  SwDecoder sw;
  DecoderHandler* head = &sw;
  if (!swOnly) { vt.setNext(&vaapi)->setNext(&cuda)->setNext(&sw); head = &vt; }
  std::printf("%s：\n", swOnly ? "只用软解" : "回退链 videotoolbox → vaapi → cuda → software");
  AVCodecContext* ctx = head->open(fmt->streams[vs]->codecpar);
  if (!ctx) return 1;
  DecoderHandler* owner = ctx->hw_device_ctx ? static_cast<DecoderHandler*>(ctx->opaque) : &sw;

  auto t = Clock::now();
  double cpu0 = cpuMs();
  AVPacket* pkt = av_packet_alloc();
  AVFrame* frame = av_frame_alloc();
  AVFrame* cpu = av_frame_alloc();
  int frames = 0, hw = 0;
  auto drain = [&] {
    while (avcodec_receive_frame(ctx, frame) == 0) {
      ++frames;
      if (owner->hwFrame(frame)) { ++hw; av_hwframe_transfer_data(cpu, frame, 0); av_frame_unref(cpu); }  // 硬解的帧在显存里，拷回内存才算数
      av_frame_unref(frame);
    }
  };
  while (av_read_frame(fmt, pkt) >= 0) {
    if (pkt->stream_index == vs && avcodec_send_packet(ctx, pkt) >= 0) drain();
    av_packet_unref(pkt);
  }
  avcodec_send_packet(ctx, nullptr);
  drain();
  std::printf("解码 %d 帧（其中硬解 %d 帧），用时 %.1f ms，CPU 时间 %.1f ms\n", frames, hw, msSince(t), cpuMs() - cpu0);
  av_frame_free(&cpu); av_frame_free(&frame); av_packet_free(&pkt);
  avcodec_free_context(&ctx);
  avformat_close_input(&fmt);
}
