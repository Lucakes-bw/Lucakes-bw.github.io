// 《装饰器模式》：用继承给数据源加功能，4 种功能的每种组合都要一个子类（15 个）
#include <cstdio>
#include <memory>
#include <vector>

struct Packet {
    int index = 0;
    long long pts = 0;                 // 时间戳，单位 1/12288 秒（和 bbb-10s.mp4 的视频流一样，每帧 512）
    bool key = false;                  // 关键帧
    std::vector<unsigned char> data;
};

// ---------- 基类：模拟测试视频的 240 个视频包 ----------
class MemorySource {
public:
    explicit MemorySource(int n = 240) : n_(n) {}
    virtual ~MemorySource() = default;
    virtual bool read(Packet& p) {
        if (i_ >= n_) return false;
        p.index = i_;
        p.pts = i_ * 512LL;
        p.key = i_ % 48 == 0;                    // 每 48 帧一个关键帧
        int size = p.key ? 9000 : 1200 + i_ * 37 % 800;
        if (i_ % 60 == 59) size = 0;             // 第 59、119、179、239 个包是坏的（空包）
        p.data.assign(size, static_cast<unsigned char>(i_));
        ++i_;
        return true;
    }
private:
    int n_, i_ = 0;
};

// ---------- 每种功能组合一个子类（类名从外往里读） ----------
class LoggedSource : public MemorySource {
public:
    bool read(Packet& p) override {
        if (!MemorySource::read(p)) return false;
        if (p.key) std::printf("  [log] #%d 关键帧 pts=%.2fs %zu 字节\n", p.index, p.pts / 12288.0, p.data.size());
        return true;
    }
};

class ChecksumSource : public MemorySource {
public:
    bool read(Packet& p) override {
        if (!MemorySource::read(p)) { std::printf("  [adler32] %08x\n", (b_ << 16) | a_); return false; }
        for (unsigned char c : p.data) { a_ = (a_ + c) % 65521; b_ = (b_ + a_) % 65521; }
        return true;
    }
private:
    unsigned a_ = 1, b_ = 0;
};

class StatsSource : public MemorySource {
public:
    bool read(Packet& p) override {
        if (!MemorySource::read(p)) { std::printf("  [stats] %d 个包，%zu 字节\n", packets_, bytes_); return false; }
        ++packets_;
        bytes_ += p.data.size();
        return true;
    }
private:
    int packets_ = 0;
    size_t bytes_ = 0;
};

class ChecksumLoggedSource : public LoggedSource {
public:
    bool read(Packet& p) override {
        if (!LoggedSource::read(p)) { std::printf("  [adler32] %08x\n", (b_ << 16) | a_); return false; }
        for (unsigned char c : p.data) { a_ = (a_ + c) % 65521; b_ = (b_ + a_) % 65521; }
        return true;
    }
private:
    unsigned a_ = 1, b_ = 0;
};

class StatsLoggedSource : public LoggedSource {
public:
    bool read(Packet& p) override {
        if (!LoggedSource::read(p)) { std::printf("  [stats] %d 个包，%zu 字节\n", packets_, bytes_); return false; }
        ++packets_;
        bytes_ += p.data.size();
        return true;
    }
private:
    int packets_ = 0;
    size_t bytes_ = 0;
};

class StatsChecksumSource : public ChecksumSource {
public:
    bool read(Packet& p) override {
        if (!ChecksumSource::read(p)) { std::printf("  [stats] %d 个包，%zu 字节\n", packets_, bytes_); return false; }
        ++packets_;
        bytes_ += p.data.size();
        return true;
    }
private:
    int packets_ = 0;
    size_t bytes_ = 0;
};

class StatsChecksumLoggedSource : public ChecksumLoggedSource {
public:
    bool read(Packet& p) override {
        if (!ChecksumLoggedSource::read(p)) { std::printf("  [stats] %d 个包，%zu 字节\n", packets_, bytes_); return false; }
        ++packets_;
        bytes_ += p.data.size();
        return true;
    }
private:
    int packets_ = 0;
    size_t bytes_ = 0;
};

class ValidatedSource : public MemorySource {
public:
    bool read(Packet& p) override {
        while (MemorySource::read(p)) {
            if (!p.data.empty()) return true;
            ++dropped_;                          // 丢掉空包
        }
        return false;
    }
private:
    int dropped_ = 0;
};

class LoggedValidatedSource : public ValidatedSource {
public:
    bool read(Packet& p) override {
        if (!ValidatedSource::read(p)) return false;
        if (p.key) std::printf("  [log] #%d 关键帧 pts=%.2fs %zu 字节\n", p.index, p.pts / 12288.0, p.data.size());
        return true;
    }
};

class ChecksumValidatedSource : public ValidatedSource {
public:
    bool read(Packet& p) override {
        if (!ValidatedSource::read(p)) { std::printf("  [adler32] %08x\n", (b_ << 16) | a_); return false; }
        for (unsigned char c : p.data) { a_ = (a_ + c) % 65521; b_ = (b_ + a_) % 65521; }
        return true;
    }
private:
    unsigned a_ = 1, b_ = 0;
};

class StatsValidatedSource : public ValidatedSource {
public:
    bool read(Packet& p) override {
        if (!ValidatedSource::read(p)) { std::printf("  [stats] %d 个包，%zu 字节\n", packets_, bytes_); return false; }
        ++packets_;
        bytes_ += p.data.size();
        return true;
    }
private:
    int packets_ = 0;
    size_t bytes_ = 0;
};

class ChecksumLoggedValidatedSource : public LoggedValidatedSource {
public:
    bool read(Packet& p) override {
        if (!LoggedValidatedSource::read(p)) { std::printf("  [adler32] %08x\n", (b_ << 16) | a_); return false; }
        for (unsigned char c : p.data) { a_ = (a_ + c) % 65521; b_ = (b_ + a_) % 65521; }
        return true;
    }
private:
    unsigned a_ = 1, b_ = 0;
};

class StatsLoggedValidatedSource : public LoggedValidatedSource {
public:
    bool read(Packet& p) override {
        if (!LoggedValidatedSource::read(p)) { std::printf("  [stats] %d 个包，%zu 字节\n", packets_, bytes_); return false; }
        ++packets_;
        bytes_ += p.data.size();
        return true;
    }
private:
    int packets_ = 0;
    size_t bytes_ = 0;
};

class StatsChecksumValidatedSource : public ChecksumValidatedSource {
public:
    bool read(Packet& p) override {
        if (!ChecksumValidatedSource::read(p)) { std::printf("  [stats] %d 个包，%zu 字节\n", packets_, bytes_); return false; }
        ++packets_;
        bytes_ += p.data.size();
        return true;
    }
private:
    int packets_ = 0;
    size_t bytes_ = 0;
};

class StatsChecksumLoggedValidatedSource : public ChecksumLoggedValidatedSource {
public:
    bool read(Packet& p) override {
        if (!ChecksumLoggedValidatedSource::read(p)) { std::printf("  [stats] %d 个包，%zu 字节\n", packets_, bytes_); return false; }
        ++packets_;
        bytes_ += p.data.size();
        return true;
    }
private:
    int packets_ = 0;
    size_t bytes_ = 0;
};

int main() {
    // 想要「统计 + 校验 + 日志（+ 丢掉坏包）」：只能用正好有这几样功能的那个子类
    StatsChecksumLoggedValidatedSource src;
    Packet p;
    while (src.read(p)) {}
}
