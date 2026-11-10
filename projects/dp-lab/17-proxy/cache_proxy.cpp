// 《代理模式》：缓存代理——CachingProbe 记住最近查过的视频信息（LRU），实测不同容量下的命中率
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

// 真实对象：每次都要打开文件、解析容器头（慢）。这里只数调用次数
class FileProbe final : public MetadataService {
public:
    MediaInfo probe(const std::string& path) override {
        ++calls;
        return {1280, 720, 10000 + static_cast<int>(path.size())};
    }
    int calls = 0;
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

int main() {
    auto req = make_requests(100, 10000);
    std::printf("容量  命中率   真正打开文件的次数\n");
    for (std::size_t cap : {0, 5, 10, 20, 50, 100}) {
        FileProbe real;
        CachingProbe cache(real, cap);
        for (auto& p : req) cache.probe(p);
        std::printf("%4zu  %5.1f%%   %5d\n", cap, 100.0 * cache.hits / static_cast<double>(req.size()), real.calls);
    }
}
