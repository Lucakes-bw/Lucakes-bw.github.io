// 《工厂方法》：简单工厂——把散落各处的 new 收拢到一个函数里
#include <iostream>
#include <memory>
#include <string>

class Parser {
public:
    virtual ~Parser() = default;
    virtual std::string describe() const = 0;
};
class Mp4Parser : public Parser { public: std::string describe() const override { return "MP4：逐个读取 box"; } };
class H264Parser : public Parser { public: std::string describe() const override { return "H.264：按起始码切分 NAL"; } };
class AacParser : public Parser { public: std::string describe() const override { return "AAC：逐帧读取 ADTS 头"; } };

// 简单工厂：调用者只给出格式名，拿到一个 Parser 接口；具体创建哪个类，只有这里知道
std::unique_ptr<Parser> createParser(const std::string& format) {
    if (format == "mp4") return std::make_unique<Mp4Parser>();
    if (format == "h264") return std::make_unique<H264Parser>();
    if (format == "aac") return std::make_unique<AacParser>();
    return nullptr;
}

int main() {
    for (const char* name : {"mp4", "h264", "aac", "flv"}) {
        auto p = createParser(name);
        std::cout << name << " → " << (p ? p->describe() : "不支持的格式") << '\n';
    }
}
