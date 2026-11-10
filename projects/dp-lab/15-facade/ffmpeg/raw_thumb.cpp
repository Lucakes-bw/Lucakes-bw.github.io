// 《外观模式》：不用外观，直接调 FFmpeg 的 C API 把视频第 N 秒存成 PNG 缩略图
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include <cerrno>
#include <cstdio>
#include <cstdlib>

// 把 in 的第 sec 秒缩放到宽 width，存成 PNG；成功返回 0，失败返回 FFmpeg 的错误码
int save_thumbnail_raw(const char* in, double sec, int width, const char* out) {
    AVFormatContext* fmt = nullptr;
    AVCodecContext *dec = nullptr, *enc = nullptr;
    SwsContext* sws = nullptr;
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* rgb = av_frame_alloc();
    FILE* fp = nullptr;
    const AVCodec *codec, *png;
    AVStream* st;
    int ret, vs, height;
    int64_t target;
    bool got = false;
    if (!pkt || !frame || !rgb) { ret = AVERROR(ENOMEM); goto end; }

    // 1. 解封装：打开文件，读流信息，找到视频流
    if ((ret = avformat_open_input(&fmt, in, nullptr, nullptr)) < 0) goto end;
    if ((ret = avformat_find_stream_info(fmt, nullptr)) < 0) goto end;
    if ((ret = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0)) < 0) goto end;
    vs = ret;
    st = fmt->streams[vs];

    // 2. 打开解码器
    if (!(dec = avcodec_alloc_context3(codec))) { ret = AVERROR(ENOMEM); goto end; }
    if ((ret = avcodec_parameters_to_context(dec, st->codecpar)) < 0) goto end;
    if ((ret = avcodec_open2(dec, codec, nullptr)) < 0) goto end;

    // 3. seek 只能落到目标之前的关键帧，所以要一直解码到 pts >= 目标
    target = (int64_t)(sec / av_q2d(st->time_base));
    if ((ret = av_seek_frame(fmt, vs, target, AVSEEK_FLAG_BACKWARD)) < 0) goto end;
    while (!got) {
        ret = av_read_frame(fmt, pkt);
        if (ret == AVERROR_EOF) {
            ret = avcodec_send_packet(dec, nullptr);           // 读完了：冲刷解码器里剩下的帧
        } else if (ret < 0) {
            goto end;
        } else {
            ret = pkt->stream_index == vs ? avcodec_send_packet(dec, pkt) : 0;
            av_packet_unref(pkt);
        }
        if (ret < 0) goto end;
        while ((ret = avcodec_receive_frame(dec, frame)) >= 0) {
            if (frame->best_effort_timestamp >= target) { got = true; break; }
            av_frame_unref(frame);
        }
        if (!got && ret != AVERROR(EAGAIN)) goto end;          // AVERROR_EOF：要的时间超出了视频长度
    }

    // 4. 缩放并转换成 RGB24
    height = width * frame->height / frame->width / 2 * 2;
    sws = sws_getContext(frame->width, frame->height, (AVPixelFormat)frame->format,
                         width, height, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws) { ret = AVERROR(EINVAL); goto end; }
    rgb->format = AV_PIX_FMT_RGB24;
    rgb->width = width;
    rgb->height = height;
    if ((ret = av_frame_get_buffer(rgb, 0)) < 0) goto end;
    sws_scale(sws, frame->data, frame->linesize, 0, frame->height, rgb->data, rgb->linesize);

    // 5. 编码成 PNG
    png = avcodec_find_encoder(AV_CODEC_ID_PNG);
    if (!png || !(enc = avcodec_alloc_context3(png))) { ret = AVERROR(ENOMEM); goto end; }
    enc->width = width;
    enc->height = height;
    enc->pix_fmt = AV_PIX_FMT_RGB24;
    enc->time_base = AVRational{1, 25};
    if ((ret = avcodec_open2(enc, png, nullptr)) < 0) goto end;
    if ((ret = avcodec_send_frame(enc, rgb)) < 0) goto end;
    if ((ret = avcodec_receive_packet(enc, pkt)) < 0) goto end;

    // 6. 写文件
    if (!(fp = std::fopen(out, "wb"))) { ret = AVERROR(errno); goto end; }
    std::fwrite(pkt->data, 1, pkt->size, fp);
    ret = 0;

end:  // 7. 不管从哪一步失败跳过来，都按相反的顺序释放
    if (fp) std::fclose(fp);
    avcodec_free_context(&enc);
    sws_freeContext(sws);
    av_frame_free(&rgb);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&dec);
    avformat_close_input(&fmt);
    return ret;
}

#ifndef NO_MAIN
int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "用法：%s 视频 秒 输出.png\n", argv[0]); return 1; }
    int ret = save_thumbnail_raw(argv[1], std::atof(argv[2]), 320, argv[3]);
    if (ret < 0) {
        char msg[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, msg, sizeof msg);
        std::fprintf(stderr, "失败：%s\n", msg);
        return 1;
    }
    std::printf("写出 %s\n", argv[3]);
}
#endif
