// 《UML 类图》第 5 节：照着这段代码画出类图
// 一个极简的「播放器」：Player 组合 Decoder 和帧队列，聚合 Renderer，依赖 Logger
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

struct Frame {
    int64_t pts;
};

class Logger {
public:
    void info(const std::string& msg) { std::cout << "[info] " << msg << '\n'; }
};

class Renderer {                                   // 接口：只有纯虚函数
public:
    virtual ~Renderer() = default;
    virtual void draw(const Frame& f) = 0;
};

class SdlRenderer : public Renderer {              // 实现接口
public:
    void draw(const Frame& f) override { std::cout << "draw frame pts=" << f.pts << '\n'; }
};

class Decoder {
public:
    Frame decode(int i) { return Frame{i * 40}; }  // 每帧 40ms（25fps）
};

class Player {
public:
    explicit Player(std::shared_ptr<Renderer> r)
        : decoder_(std::make_unique<Decoder>()), renderer_(std::move(r)) {}

    void play(const std::string& url, Logger& log) {   // Logger 只在这个函数里用一下：依赖
        log.info("open " + url);
        for (int i = 0; i < 3; ++i) queue_.push_back(decoder_->decode(i));
        for (const Frame& f : queue_) renderer_->draw(f);
        log.info("done, " + std::to_string(queue_.size()) + " frames");
    }

private:
    std::unique_ptr<Decoder> decoder_;      // 组合：Player 自己创建、独占
    std::shared_ptr<Renderer> renderer_;    // 聚合：从外面传进来，可能和别人共享
    std::vector<Frame> queue_;              // 组合：0 到多个 Frame
};

int main() {
    auto screen = std::make_shared<SdlRenderer>();
    Logger log;
    Player player(screen);
    player.play("bbb-10s.mp4", log);
}
