// 《抽象工厂》：一个工厂负责创建一整套配套的对象，按运行环境选一套
#include <iostream>
#include <memory>
#include <string>
#include <vector>

enum class Memory { Cpu, VideoToolbox, Vaapi };
struct Frame { Memory memory; int index; };

class Decoder {
public:
    virtual ~Decoder() = default;
    virtual Frame decode(int i) = 0;
};
class Renderer {
public:
    virtual ~Renderer() = default;
    virtual bool draw(const Frame& f) = 0;
};

// ---------- 抽象工厂：规定「一套后端」要提供哪些产品 ----------
struct Env { bool hasVideoToolbox; bool hasVaapi; };   // 运行环境：有哪些硬件加速可用

class MediaBackend {
public:
    virtual ~MediaBackend() = default;
    virtual std::string name() const = 0;
    virtual bool available(const Env& env) const = 0;
    virtual std::unique_ptr<Decoder> createDecoder() const = 0;
    virtual std::unique_ptr<Renderer> createRenderer() const = 0;
};

// ---------- 三套具体的后端，每套内部的产品是配套的 ----------
class SoftwareBackend : public MediaBackend {
    struct Dec : Decoder { Frame decode(int i) override { return {Memory::Cpu, i}; } };
    struct Ren : Renderer { bool draw(const Frame& f) override { return f.memory == Memory::Cpu; } };
public:
    std::string name() const override { return "软件（FFmpeg 软解 + SDL）"; }
    bool available(const Env&) const override { return true; }                 // 软件后端哪里都能用
    std::unique_ptr<Decoder> createDecoder() const override { return std::make_unique<Dec>(); }
    std::unique_ptr<Renderer> createRenderer() const override { return std::make_unique<Ren>(); }
};

class VideoToolboxBackend : public MediaBackend {
    struct Dec : Decoder { Frame decode(int i) override { return {Memory::VideoToolbox, i}; } };
    struct Ren : Renderer { bool draw(const Frame& f) override { return f.memory == Memory::VideoToolbox; } };
public:
    std::string name() const override { return "VideoToolbox 硬解 + Metal"; }
    bool available(const Env& e) const override { return e.hasVideoToolbox; }
    std::unique_ptr<Decoder> createDecoder() const override { return std::make_unique<Dec>(); }
    std::unique_ptr<Renderer> createRenderer() const override { return std::make_unique<Ren>(); }
};

class VaapiBackend : public MediaBackend {
    struct Dec : Decoder { Frame decode(int i) override { return {Memory::Vaapi, i}; } };
    struct Ren : Renderer { bool draw(const Frame& f) override { return f.memory == Memory::Vaapi; } };
public:
    std::string name() const override { return "VA-API 硬解 + EGL"; }
    bool available(const Env& e) const override { return e.hasVaapi; }
    std::unique_ptr<Decoder> createDecoder() const override { return std::make_unique<Dec>(); }
    std::unique_ptr<Renderer> createRenderer() const override { return std::make_unique<Ren>(); }
};

// 按优先级挑第一套可用的后端：硬件优先，软件兜底
const MediaBackend& chooseBackend(const Env& env) {
    static const VideoToolboxBackend vt;
    static const VaapiBackend vaapi;
    static const SoftwareBackend sw;
    for (const MediaBackend* b : std::vector<const MediaBackend*>{&vt, &vaapi, &sw})
        if (b->available(env)) return *b;
    return sw;
}

// 播放器只认识抽象工厂和抽象产品；两个产品出自同一个工厂，一定配套
void play(const MediaBackend& backend, int frames) {
    auto decoder = backend.createDecoder();
    auto renderer = backend.createRenderer();
    int drawn = 0;
    for (int i = 1; i <= frames; ++i) drawn += renderer->draw(decoder->decode(i));
    std::cout << "  后端：" << backend.name() << "，画出 " << drawn << " / " << frames << " 帧\n";
}

int main() {
    struct { const char* desc; Env env; } machines[] = {
        {"MacBook", {true, false}},
        {"带 Intel 核显的 Linux 台式机", {false, true}},
        {"没有 GPU 的 Linux 服务器", {false, false}},
    };
    for (const auto& m : machines) {
        std::cout << m.desc << '\n';
        play(chooseBackend(m.env), 240);
    }
}
