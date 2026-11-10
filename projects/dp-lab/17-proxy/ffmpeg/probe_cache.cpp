// 《代理模式》：缓存代理的耗时——真实对象用 FFmpeg 打开文件读流信息，比较不同缓存容量下每次请求的平均耗时
extern "C" {
#include <libavformat/avformat.h>
}
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <list>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct MediaInfo { int width = 0, height = 0, duration_ms = 0; };

struct MetadataService {
    virtual ~MetadataService() = default;
    virtual MediaInfo probe(const std::string& path) = 0;
};

// 真实对象：用 FFmpeg 打开文件、读流信息。100 个名字都映射到同一个测试视频
class FileProbe final : public MetadataService {
public:
    explicit FileProbe(std::string video) : video_(std::move(video)) {}
    MediaInfo probe(const std::string&) override {
        ++calls;
        MediaInfo info;
        AVFormatContext* fmt = nullptr;
        if (avformat_open_input(&fmt, video_.c_str(), nullptr, nullptr) < 0) return info;
        if (avformat_find_stream_info(fmt, nullptr) >= 0) {
            int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
            if (vs >= 0) {
                info.width = fmt->streams[vs]->codecpar->width;
                info.height = fmt->streams[vs]->codecpar->height;
            }
            info.duration_ms = static_cast<int>(fmt->duration / 1000);
        }
        avformat_close_input(&fmt);
        return info;
    }
    int calls = 0;

private:
    std::string video_;
};

// 缓存代理：最多记住 capacity 条，满了就扔掉最久没用过的那条
class CachingProbe final : public MetadataService {
public:
    CachingProbe(MetadataService& real, std::size_t capacity) : real_(real), capacity_(capacity) {}
    MediaInfo probe(const std::string& path) override {
        if (auto it = index_.find(path); it != index_.end()) {
            ++hits;
            order_.splice(order_.begin(), order_, it->second);   // 挪到最前面：刚用过
            return it->second->second;
        }
        ++misses;
        MediaInfo info = real_.probe(path);
        if (capacity_ == 0) return info;
        if (order_.size() == capacity_) {                        // 满了：淘汰最后一条
            index_.erase(order_.back().first);
            order_.pop_back();
        }
        order_.emplace_front(path, info);
        index_[path] = order_.begin();
        return info;
    }
    int hits = 0, misses = 0;

private:
    using Entry = std::pair<std::string, MediaInfo>;
    MetadataService& real_;
    std::size_t capacity_;
    std::list<Entry> order_;                                     // 最近用过的在前面
    std::unordered_map<std::string, std::list<Entry>::iterator> index_;
};

// 请求序列：100 个文件，热门的文件被查得多（Zipf 分布，第 k 热门的概率正比于 1/k）
// 只用 mt19937 的原始输出（标准规定了它的序列），保证 libc++ 和 libstdc++ 得到同一串请求
std::vector<std::string> make_requests(int files, int n) {
    std::vector<double> cdf;
    double sum = 0;
    for (int k = 1; k <= files; ++k) cdf.push_back(sum += 1.0 / k);
    std::mt19937 rng(42);
    std::vector<std::string> req;
    for (int i = 0; i < n; ++i) {
        double u = rng() / 4294967296.0 * sum;
        int k = 0;
        while (cdf[static_cast<std::size_t>(k)] <= u) ++k;
        req.push_back("clip" + std::to_string(k) + ".mp4");
    }
    return req;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "用法：%s 视频\n", argv[0]); return 1; }
    auto req = make_requests(100, 1000);
    std::printf("容量  命中率   每次请求（中位数，7 次）\n");
    for (std::size_t cap : {0, 10, 20, 50, 100}) {
        std::vector<double> us;
        int hits = 0;
        for (int r = 0; r < 7; ++r) {
            FileProbe real(argv[1]);
            CachingProbe cache(real, cap);
            int w = 0;
            auto t0 = std::chrono::steady_clock::now();
            for (auto& p : req) w += cache.probe(p).width;
            us.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / req.size());
            hits = cache.hits;
            if (w == 0) return 1;
        }
        std::sort(us.begin(), us.end());
        std::printf("%4zu  %5.1f%%   %8.1f µs\n", cap, 100.0 * hits / static_cast<double>(req.size()), us[3]);
    }
}
