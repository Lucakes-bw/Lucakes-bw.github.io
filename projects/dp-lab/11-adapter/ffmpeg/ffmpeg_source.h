// 《适配器》：对象适配器——把 FFmpeg 的 C API 适配成 MediaSource
#pragma once
#include "media_source.h"

struct AVFormatContext;
struct AVPacket;

class FfmpegSource : public MediaSource {
public:
    // 打开失败时返回空指针，并在 error 里写明原因（FFmpeg 的错误码翻译成文字）
    static std::unique_ptr<FfmpegSource> open(const std::string& path, std::string& error);

    std::string describe() const override;
    std::optional<Packet> next() override;

private:
    struct FormatCloser { void operator()(AVFormatContext* c) const; };
    struct PacketFreer { void operator()(AVPacket* p) const; };

    FfmpegSource() = default;
    std::unique_ptr<AVFormatContext, FormatCloser> fmt_;   // 被适配的对象，析构时自动 avformat_close_input
    std::unique_ptr<AVPacket, PacketFreer> pkt_;           // 复用的 AVPacket，析构时自动 av_packet_free
};
