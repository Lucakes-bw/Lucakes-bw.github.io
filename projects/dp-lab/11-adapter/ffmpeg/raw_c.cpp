// 《适配器》：直接用 FFmpeg 的 C API——找到就 return，忘了释放
extern "C" {
#include <libavformat/avformat.h>
}
#include <cstdio>

// 找到 sec 秒之后的第一个视频关键帧（比如生成缩略图），返回它的时间；找不到返回 -1
double keyframeAfter(const char* path, double sec) {
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path, nullptr, nullptr) < 0) return -1;
    if (avformat_find_stream_info(fmt, nullptr) < 0) return -1;            // 漏了 avformat_close_input
    int v = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    AVPacket* pkt = av_packet_alloc();
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == v && (pkt->flags & AV_PKT_FLAG_KEY)) {
            double t = pkt->pts * av_q2d(fmt->streams[v]->time_base);
            if (t >= sec) return t;                // 找到了就返回：忘了 av_packet_unref、av_packet_free、avformat_close_input
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    avformat_close_input(&fmt);
    return -1;
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "bbb-10s.mp4";
    for (int s = 0; s < 10; ++s) std::printf("%.2f ", keyframeAfter(path, s));   // 每秒生成一张缩略图
    std::printf("\n");
}
