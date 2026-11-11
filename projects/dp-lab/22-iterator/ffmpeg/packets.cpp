// 《迭代器模式》：把 FFmpeg 的两个「迭代」接口包成 C++ 的范围——av_read_frame 读包（输入迭代器）、av_codec_iterate 列编解码器
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <iterator>

// 1. 文件里的包：av_read_frame 每调用一次读出下一个包，读完返回负数——天生就是只能走一遍的输入迭代器
class Packets {
public:
    explicit Packets(AVFormatContext* fmt) : fmt_(fmt), pkt_(av_packet_alloc()) {}
    ~Packets() { av_packet_free(&pkt_); }
    Packets(const Packets&) = delete;
    Packets& operator=(const Packets&) = delete;

    class iterator {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = AVPacket;
        using difference_type = std::ptrdiff_t;
        using pointer = const AVPacket*;
        using reference = const AVPacket&;
        iterator() = default;
        explicit iterator(Packets* p) : p_(p) { ++*this; }
        reference operator*() const { return *p_->pkt_; }
        pointer operator->() const { return p_->pkt_; }
        iterator& operator++() {
            av_packet_unref(p_->pkt_);                          // 上一个包的数据在这里释放
            if (av_read_frame(p_->fmt_, p_->pkt_) < 0) p_ = nullptr;
            return *this;
        }
        void operator++(int) { ++*this; }
        friend bool operator==(const iterator& a, const iterator& b) { return a.p_ == b.p_; }
        friend bool operator!=(const iterator& a, const iterator& b) { return a.p_ != b.p_; }
    private:
        Packets* p_ = nullptr;
    };
    iterator begin() { return iterator(this); }
    iterator end() { return iterator(); }

private:
    AVFormatContext* fmt_;
    AVPacket* pkt_;
};

// 2. 所有编解码器：av_codec_iterate(&opaque) 用一个 void* 记住「走到哪了」，返回 NULL 表示结束
class Codecs {
public:
    class iterator {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = const AVCodec*;
        using difference_type = std::ptrdiff_t;
        using pointer = const AVCodec* const*;
        using reference = const AVCodec* const&;
        iterator() = default;
        explicit iterator(bool) { ++*this; }
        reference operator*() const { return cur_; }
        iterator& operator++() { cur_ = av_codec_iterate(&opaque_); return *this; }
        void operator++(int) { ++*this; }
        friend bool operator==(const iterator& a, const iterator& b) { return a.cur_ == b.cur_; }
        friend bool operator!=(const iterator& a, const iterator& b) { return a.cur_ != b.cur_; }
    private:
        void* opaque_ = nullptr;
        const AVCodec* cur_ = nullptr;
    };
    iterator begin() const { return iterator(true); }
    iterator end() const { return iterator(); }
};

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "../../../../source/media/bbb-10s.mp4";
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path, nullptr, nullptr) < 0 || avformat_find_stream_info(fmt, nullptr) < 0) {
        std::printf("打不开 %s\n", path);
        return 1;
    }
    int count[8] = {}, keys = 0;
    long long bytes = 0;
    {
        Packets packets(fmt);
        for (const AVPacket& p : packets) {
            if (p.stream_index < 8) ++count[p.stream_index];
            if (p.flags & AV_PKT_FLAG_KEY && fmt->streams[p.stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) ++keys;
            bytes += p.size;
        }
    }
    for (unsigned i = 0; i < fmt->nb_streams && i < 8; ++i)
        std::printf("流 %u（%s）：%d 个包\n", i, av_get_media_type_string(fmt->streams[i]->codecpar->codec_type), count[i]);
    std::printf("视频关键帧 %d 个，包数据共 %lld 字节\n", keys, bytes);
    avformat_close_input(&fmt);

    Codecs codecs;
    auto decoders = std::count_if(codecs.begin(), codecs.end(), [](const AVCodec* c) { return av_codec_is_decoder(c); });
    auto h264 = std::find_if(codecs.begin(), codecs.end(), [](const AVCodec* c) { return c->id == AV_CODEC_ID_H264 && av_codec_is_decoder(c); });
    std::printf("这个 FFmpeg 有 %td 个解码器；第一个 H.264 解码器：%s\n", decoders, h264 != codecs.end() ? (*h264)->name : "无");
}
