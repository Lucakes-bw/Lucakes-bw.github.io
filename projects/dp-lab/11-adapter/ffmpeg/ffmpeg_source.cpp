extern "C" {
#include <libavformat/avformat.h>
}
#include "ffmpeg_source.h"

void FfmpegSource::FormatCloser::operator()(AVFormatContext* c) const { avformat_close_input(&c); }
void FfmpegSource::PacketFreer::operator()(AVPacket* p) const { av_packet_free(&p); }

static std::string errorText(int code) {          // FFmpeg 返回负数错误码，翻译成文字
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(code, buf, sizeof buf);
    return buf;
}

std::unique_ptr<FfmpegSource> FfmpegSource::open(const std::string& path, std::string& error) {
    AVFormatContext* raw = nullptr;
    if (int rc = avformat_open_input(&raw, path.c_str(), nullptr, nullptr); rc < 0) {
        error = "打开 " + path + " 失败：" + errorText(rc);
        return nullptr;
    }
    std::unique_ptr<FfmpegSource> src(new FfmpegSource);
    src->fmt_.reset(raw);                          // 从这里开始，任何一条返回路径都会自动关闭文件
    if (int rc = avformat_find_stream_info(raw, nullptr); rc < 0) {
        error = "读取流信息失败：" + errorText(rc);
        return nullptr;
    }
    src->pkt_.reset(av_packet_alloc());
    return src;
}

std::string FfmpegSource::describe() const {
    return std::string(fmt_->iformat->name) + "，" + std::to_string(fmt_->nb_streams) + " 路流，时长 " +
           std::to_string(fmt_->duration / 1000) + " ms";
}

std::optional<Packet> FfmpegSource::next() {
    if (av_read_frame(fmt_.get(), pkt_.get()) < 0) return std::nullopt;   // 文件结束或出错
    AVPacket* p = pkt_.get();
    AVStream* st = fmt_->streams[p->stream_index];
    Packet out{p->stream_index, st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO, p->pts * av_q2d(st->time_base),
               (p->flags & AV_PKT_FLAG_KEY) != 0, std::vector<uint8_t>(p->data, p->data + p->size)};
    av_packet_unref(p);                            // C API 要求每读一个包都手动释放
    return out;
}
