// 《模板方法模式》：用真正的 FFmpeg 导出 MP4、GIF、WAV——基类的 run() 是整套流程，子类只配置编码器、转换格式
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

class ExportJob {
public:
    virtual ~ExportJob() = default;

    bool run(const std::string& in, const std::string& out) {   // 模板方法：公有、非虚
        auto t0 = std::chrono::steady_clock::now();
        bool ok = openInput(in) && openOutput(out) && transcode();
        if (ok) bytes_ = avio_tell(oc_->pb);
        releaseConverter();                      // 钩子：子类释放自己的资源
        cleanup();                               // 公共资源永远由基类释放，成功失败都走这里
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("%-8s 解码 %3d 帧，编码 %3d 帧，%8ld 字节，%7.1f ms%s\n", out.c_str() + out.rfind('/') + 1,
                    decoded_, encoded_, bytes_, ms, ok ? "" : "  失败");
        return ok;
    }

protected:
    // 基本操作：子类必须实现
    virtual AVMediaType mediaType() const = 0;
    virtual const char* encoderName() const = 0;
    virtual void configure(AVCodecContext* enc, const AVCodecContext* dec, AVRational fps) = 0;
    virtual AVFrame* convert(AVFrame* in, const AVCodecContext* enc) = 0;   // 转成编码器要的格式
    // 钩子：有默认实现
    virtual bool keep(int /*index*/) { return true; }                       // 返回 false 丢掉这一帧
    virtual void releaseConverter() {}

private:
    AVFormatContext *ic_ = nullptr, *oc_ = nullptr;
    AVCodecContext *dec_ = nullptr, *enc_ = nullptr;
    AVStream* ost_ = nullptr;
    int stream_ = -1, decoded_ = 0, encoded_ = 0;
    int64_t nextPts_ = 0;
    long bytes_ = 0;

    bool openInput(const std::string& in) {
        if (avformat_open_input(&ic_, in.c_str(), nullptr, nullptr) < 0) return false;
        if (avformat_find_stream_info(ic_, nullptr) < 0) return false;
        const AVCodec* dc = nullptr;
        stream_ = av_find_best_stream(ic_, mediaType(), -1, -1, &dc, 0);
        if (stream_ < 0) return false;
        dec_ = avcodec_alloc_context3(dc);
        avcodec_parameters_to_context(dec_, ic_->streams[stream_]->codecpar);
        dec_->pkt_timebase = ic_->streams[stream_]->time_base;
        return avcodec_open2(dec_, dc, nullptr) >= 0;
    }

    bool openOutput(const std::string& out) {
        if (avformat_alloc_output_context2(&oc_, nullptr, nullptr, out.c_str()) < 0) return false;
        const AVCodec* ec = avcodec_find_encoder_by_name(encoderName());
        if (!ec) { std::fprintf(stderr, "没有编码器 %s\n", encoderName()); return false; }
        enc_ = avcodec_alloc_context3(ec);
        configure(enc_, dec_, av_guess_frame_rate(ic_, ic_->streams[stream_], nullptr));
        if (oc_->oformat->flags & AVFMT_GLOBALHEADER) enc_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (avcodec_open2(enc_, ec, nullptr) < 0) return false;
        ost_ = avformat_new_stream(oc_, nullptr);
        avcodec_parameters_from_context(ost_->codecpar, enc_);
        ost_->time_base = enc_->time_base;
        if (avio_open(&oc_->pb, out.c_str(), AVIO_FLAG_WRITE) < 0) return false;
        return avformat_write_header(oc_, nullptr) >= 0;
    }

    bool transcode() {
        AVPacket* pkt = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        while (av_read_frame(ic_, pkt) >= 0) {
            if (pkt->stream_index == stream_) decode(pkt, frame);
            av_packet_unref(pkt);
        }
        decode(nullptr, frame);                  // 冲刷解码器
        encode(nullptr);                         // 冲刷编码器
        av_frame_free(&frame);
        av_packet_free(&pkt);
        return av_write_trailer(oc_) >= 0;
    }

    void decode(const AVPacket* pkt, AVFrame* frame) {
        avcodec_send_packet(dec_, pkt);
        while (avcodec_receive_frame(dec_, frame) >= 0) {
            if (keep(decoded_++)) {
                AVFrame* f = convert(frame, enc_);
                f->pts = nextPts_;
                nextPts_ += mediaType() == AVMEDIA_TYPE_AUDIO ? f->nb_samples : 1;
                encode(f);
            }
            av_frame_unref(frame);
        }
    }

    void encode(const AVFrame* f) {
        avcodec_send_frame(enc_, f);
        AVPacket* pkt = av_packet_alloc();
        while (avcodec_receive_packet(enc_, pkt) >= 0) {
            ++encoded_;
            pkt->stream_index = 0;
            av_packet_rescale_ts(pkt, enc_->time_base, ost_->time_base);
            av_interleaved_write_frame(oc_, pkt);
        }
        av_packet_free(&pkt);
    }

    void cleanup() {
        avcodec_free_context(&enc_);
        avcodec_free_context(&dec_);
        if (oc_ && oc_->pb) avio_closep(&oc_->pb);
        avformat_free_context(oc_);
        oc_ = nullptr;
        avformat_close_input(&ic_);
    }
};

class VideoExport : public ExportJob {           // 中间层：两个视频任务共用的缩放和像素格式转换
protected:
    AVMediaType mediaType() const override { return AVMEDIA_TYPE_VIDEO; }
    AVFrame* convert(AVFrame* in, const AVCodecContext* enc) override {
        sws_ = sws_getCachedContext(sws_, in->width, in->height, static_cast<AVPixelFormat>(in->format),
                                    enc->width, enc->height, enc->pix_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!out_) {
            out_ = av_frame_alloc();
            out_->format = enc->pix_fmt;
            out_->width = enc->width;
            out_->height = enc->height;
            av_frame_get_buffer(out_, 0);
        }
        av_frame_make_writable(out_);            // 编码器可能还引用着上一帧
        sws_scale(sws_, in->data, in->linesize, 0, in->height, out_->data, out_->linesize);
        return out_;
    }
    void releaseConverter() override {
        sws_freeContext(sws_);
        av_frame_free(&out_);
    }

private:
    SwsContext* sws_ = nullptr;
    AVFrame* out_ = nullptr;
};

class Mp4Export : public VideoExport {           // H.264，缩到 640×360
protected:
    const char* encoderName() const override { return "libx264"; }
    void configure(AVCodecContext* enc, const AVCodecContext*, AVRational fps) override {
        enc->width = 640;
        enc->height = 360;
        enc->pix_fmt = AV_PIX_FMT_YUV420P;
        enc->time_base = av_inv_q(fps);
        enc->framerate = fps;
        av_opt_set(enc->priv_data, "preset", "veryfast", 0);
    }
};

class GifExport : public VideoExport {           // 320×180，每 2 帧留 1 帧（24 fps → 12 fps）
protected:
    const char* encoderName() const override { return "gif"; }
    void configure(AVCodecContext* enc, const AVCodecContext*, AVRational fps) override {
        enc->width = 320;
        enc->height = 180;
        enc->pix_fmt = AV_PIX_FMT_RGB8;
        enc->time_base = av_inv_q(av_div_q(fps, AVRational{2, 1}));
    }
    bool keep(int index) override { return index % 2 == 0; }
};

class WavExport : public ExportJob {             // AAC 解码出来的浮点平面格式转成 16 位交错 PCM
protected:
    AVMediaType mediaType() const override { return AVMEDIA_TYPE_AUDIO; }
    const char* encoderName() const override { return "pcm_s16le"; }
    void configure(AVCodecContext* enc, const AVCodecContext* dec, AVRational) override {
        enc->sample_fmt = AV_SAMPLE_FMT_S16;
        enc->sample_rate = dec->sample_rate;
        av_channel_layout_copy(&enc->ch_layout, &dec->ch_layout);
        enc->time_base = AVRational{1, dec->sample_rate};
    }
    AVFrame* convert(AVFrame* in, const AVCodecContext* enc) override {
        if (!swr_) {
            swr_alloc_set_opts2(&swr_, &enc->ch_layout, enc->sample_fmt, enc->sample_rate,
                                &in->ch_layout, static_cast<AVSampleFormat>(in->format), in->sample_rate, 0, nullptr);
            swr_init(swr_);
            out_ = av_frame_alloc();
        }
        av_frame_unref(out_);
        out_->format = enc->sample_fmt;
        out_->sample_rate = enc->sample_rate;
        av_channel_layout_copy(&out_->ch_layout, &enc->ch_layout);
        out_->nb_samples = swr_get_out_samples(swr_, in->nb_samples);
        av_frame_get_buffer(out_, 0);
        out_->nb_samples = swr_convert(swr_, out_->data, out_->nb_samples,
                                       const_cast<const uint8_t**>(in->extended_data), in->nb_samples);
        return out_;
    }
    void releaseConverter() override {
        swr_free(&swr_);
        av_frame_free(&out_);
    }

private:
    SwrContext* swr_ = nullptr;
    AVFrame* out_ = nullptr;
};

int main(int argc, char** argv) {
    std::string in = argc > 1 ? argv[1] : "../../../../source/media/bbb-10s.mp4";
    std::string dir = argc > 2 ? argv[2] : "build";
    av_log_set_level(AV_LOG_ERROR);
    std::vector<std::pair<std::unique_ptr<ExportJob>, std::string>> jobs;
    jobs.emplace_back(std::make_unique<Mp4Export>(), dir + "/out.mp4");
    jobs.emplace_back(std::make_unique<GifExport>(), dir + "/out.gif");
    jobs.emplace_back(std::make_unique<WavExport>(), dir + "/out.wav");
    for (auto& [job, out] : jobs) job->run(in, out);
}
