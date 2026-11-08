// 《抽象工厂》：解码器和渲染器分别按配置创建，3 × 3 种组合里只有 3 种能用
#include <iostream>
#include <memory>
#include <string>

enum class Memory { Cpu, VideoToolbox, Vaapi };     // 解码出来的帧放在哪里
const char* where(Memory m) { return m == Memory::Cpu ? "内存" : m == Memory::VideoToolbox ? "GPU（VideoToolbox）" : "GPU（VA-API）"; }

struct Frame { Memory memory; int index; };

class Decoder {
public:
    virtual ~Decoder() = default;
    virtual Frame decode(int i) = 0;
};
class SoftwareDecoder : public Decoder { public: Frame decode(int i) override { return {Memory::Cpu, i}; } };
class VideoToolboxDecoder : public Decoder { public: Frame decode(int i) override { return {Memory::VideoToolbox, i}; } };   // macOS 硬解
class VaapiDecoder : public Decoder { public: Frame decode(int i) override { return {Memory::Vaapi, i}; } };                 // Linux 硬解

class Renderer {
public:
    virtual ~Renderer() = default;
    virtual Memory accepts() const = 0;               // 能直接画哪里的帧
    bool draw(const Frame& f) { return f.memory == accepts(); }
};
class SdlRenderer : public Renderer { public: Memory accepts() const override { return Memory::Cpu; } };
class MetalRenderer : public Renderer { public: Memory accepts() const override { return Memory::VideoToolbox; } };
class EglRenderer : public Renderer { public: Memory accepts() const override { return Memory::Vaapi; } };

// 没有抽象工厂：解码器和渲染器各自按一个配置项创建，谁也不知道对方是谁
std::unique_ptr<Decoder> makeDecoder(const std::string& name) {
    if (name == "videotoolbox") return std::make_unique<VideoToolboxDecoder>();
    if (name == "vaapi") return std::make_unique<VaapiDecoder>();
    return std::make_unique<SoftwareDecoder>();
}
std::unique_ptr<Renderer> makeRenderer(const std::string& name) {
    if (name == "metal") return std::make_unique<MetalRenderer>();
    if (name == "egl") return std::make_unique<EglRenderer>();
    return std::make_unique<SdlRenderer>();
}

int main() {
    int ok = 0;
    for (const char* dec : {"software", "videotoolbox", "vaapi"})
        for (const char* ren : {"sdl", "metal", "egl"}) {
            auto d = makeDecoder(dec);
            auto r = makeRenderer(ren);
            Frame f = d->decode(1);
            bool good = r->draw(f);
            ok += good;
            std::cout << "decoder=" << dec << " renderer=" << ren << "  "
                      << (good ? "✓" : std::string("✗ 帧在") + where(f.memory) + "，渲染器只能画" + where(r->accepts()) + "的帧") << '\n';
        }
    std::cout << "9 种组合里能用的：" << ok << " 种\n";
}
