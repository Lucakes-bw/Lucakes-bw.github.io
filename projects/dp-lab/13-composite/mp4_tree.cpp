// 《组合》：MP4 的 box 树也是组合结构——容器 box 的长度应该等于 8 字节的头加上所有子 box 的长度
//   ./mp4_tree bbb-10s.mp4
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

class Box {
public:
    Box(std::string type, uint64_t declared) : type_(std::move(type)), declared_(declared) {}
    virtual ~Box() = default;
    const std::string& type() const { return type_; }
    uint64_t declared() const { return declared_; }          // 文件里写的长度
    virtual uint64_t size() const = 0;                       // 按结构算出来的长度
    virtual void print(int depth, int /*maxDepth*/) const {
        std::printf("%*s%-4s %8llu 字节\n", depth * 2, "", type_.c_str(), (unsigned long long)size());
    }
    virtual int check() const { return 0; }                  // 返回长度对不上的容器个数
private:
    std::string type_;
    uint64_t declared_;
};

class LeafBox : public Box {                                 // 叶子：内容是数据（mdat、stsz……）
public:
    using Box::Box;
    uint64_t size() const override { return declared(); }
};

class ContainerBox : public Box {                            // 容器：内容是子 box（moov、trak……）
public:
    ContainerBox(std::string type, uint64_t declared, uint64_t header) : Box(std::move(type), declared), header_(header) {}
    void add(std::unique_ptr<Box> b) { children_.push_back(std::move(b)); }
    uint64_t size() const override {
        uint64_t sum = header_;                              // 自己的头：通常是 4 字节长度 + 4 字节类型
        for (const auto& c : children_) sum += c->size();
        return sum;
    }
    void print(int depth, int maxDepth) const override {
        Box::print(depth, maxDepth);
        if (depth + 1 < maxDepth)
            for (const auto& c : children_) c->print(depth + 1, maxDepth);
    }
    int check() const override {
        int bad = size() != declared();
        for (const auto& c : children_) bad += c->check();
        return bad;
    }
private:
    uint64_t header_;
    std::vector<std::unique_ptr<Box>> children_;
};

static uint32_t be32(const unsigned char* p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static bool isContainer(const char* t) {
    for (const char* c : {"moov", "trak", "mdia", "minf", "stbl", "dinf", "edts", "mvex", "moof", "traf"})
        if (std::memcmp(t, c, 4) == 0) return true;
    return false;
}

// 解析 [begin, end) 范围内的一串 box，加到 parent 里
static void parse(const std::vector<unsigned char>& buf, size_t begin, size_t end, ContainerBox& parent, int& count) {
    for (size_t pos = begin; pos + 8 <= end;) {
        uint64_t size = be32(&buf[pos]);
        const char* type = reinterpret_cast<const char*>(&buf[pos + 4]);
        size_t header = 8;
        if (size == 1) { size = (uint64_t)be32(&buf[pos + 8]) << 32 | be32(&buf[pos + 12]); header = 16; }
        if (size == 0) size = end - pos;
        if (size < header || pos + size > end) break;
        ++count;
        if (isContainer(type)) {
            auto c = std::make_unique<ContainerBox>(std::string(type, 4), size, header);
            parse(buf, pos + header, pos + size, *c, count);
            parent.add(std::move(c));
        } else {
            parent.add(std::make_unique<LeafBox>(std::string(type, 4), size));
        }
        pos += size;
    }
}

int main(int argc, char** argv) {
    std::ifstream in(argc > 1 ? argv[1] : "bbb-10s.mp4", std::ios::binary);
    std::vector<unsigned char> buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    ContainerBox file("文件", buf.size(), 0);               // 整个文件就是一个没有头的容器
    int count = 0;
    parse(buf, 0, buf.size(), file, count);
    file.print(0, 4);
    std::printf("共 %d 个 box；长度对不上的容器：%d 个\n", count, file.check());
}
