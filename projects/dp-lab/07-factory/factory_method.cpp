// 《工厂方法》：框架定好流程，「创建哪种解码器」留给子类决定
#include <iostream>
#include <memory>
#include <string>

class Decoder {
public:
    virtual ~Decoder() = default;
    virtual std::string decode(int frame) = 0;
};

class SoftwareDecoder : public Decoder {
public:
    std::string decode(int frame) override { return "CPU 解码第 " + std::to_string(frame) + " 帧"; }
};
class HardwareDecoder : public Decoder {
public:
    std::string decode(int frame) override { return "GPU 解码第 " + std::to_string(frame) + " 帧"; }
};

// 创建者：播放流程是固定的，但它不知道该用哪种解码器
class Player {
public:
    virtual ~Player() = default;
    void play(int frames) {                        // 固定的流程
        auto decoder = createDecoder();            // ← 工厂方法：由子类决定创建什么
        for (int i = 1; i <= frames; ++i) std::cout << "  " << decoder->decode(i) << '\n';
    }

protected:
    virtual std::unique_ptr<Decoder> createDecoder() = 0;
};

class DesktopPlayer : public Player {              // 桌面版：用 GPU 硬解
protected:
    std::unique_ptr<Decoder> createDecoder() override { return std::make_unique<HardwareDecoder>(); }
};
class ServerPlayer : public Player {               // 服务器上没有 GPU：用软解
protected:
    std::unique_ptr<Decoder> createDecoder() override { return std::make_unique<SoftwareDecoder>(); }
};

// 测试：换一个返回假解码器的子类，就能测试播放流程本身，不需要真的解码
class FakeDecoder : public Decoder {
public:
    std::string decode(int frame) override { return "假帧 " + std::to_string(frame); }
};
class TestPlayer : public Player {
protected:
    std::unique_ptr<Decoder> createDecoder() override { return std::make_unique<FakeDecoder>(); }
};

int main() {
    std::cout << "DesktopPlayer:\n";
    DesktopPlayer().play(2);
    std::cout << "ServerPlayer:\n";
    ServerPlayer().play(2);
    std::cout << "TestPlayer:\n";
    TestPlayer().play(2);
}
