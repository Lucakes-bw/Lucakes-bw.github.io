// 《桥接模式》练习 3：同一个窗口里的视图共用一个后端（一个 GPU 设备），换后端时一起换
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

class Renderer {
public:
    virtual ~Renderer() = default;
    virtual std::string name() const = 0;
    virtual void frame(const std::string& who, double t) = 0;
};

class MetalRenderer : public Renderer {
public:
    MetalRenderer() { std::printf("  创建 Metal 设备\n"); }
    ~MetalRenderer() override { std::printf("  释放 Metal 设备\n"); }
    std::string name() const override { return "Metal"; }
    void frame(const std::string& who, double t) override { std::printf("  Metal 画 %s @%.1f\n", who.c_str(), t); }
};

class SoftRenderer : public Renderer {
public:
    SoftRenderer() { std::printf("  创建软件画布\n"); }
    ~SoftRenderer() override { std::printf("  释放软件画布\n"); }
    std::string name() const override { return "软件"; }
    void frame(const std::string& who, double t) override { std::printf("  软件 画 %s @%.1f\n", who.c_str(), t); }
};

class VideoView {
public:
    VideoView(std::string name, std::shared_ptr<Renderer> r) : name_(std::move(name)), r_(std::move(r)) {}
    void setRenderer(std::shared_ptr<Renderer> r) { r_ = std::move(r); }
    void draw(double t) { r_->frame(name_, t); }
private:
    std::string name_;
    std::shared_ptr<Renderer> r_;                     // 多个视图共用一个后端
};

class PlayerWindow {                                  // 窗口拥有所有视图，负责「一起换」
public:
    explicit PlayerWindow(std::shared_ptr<Renderer> r) : r_(r) {
        views_.emplace_back("主窗口", r);
        views_.emplace_back("缩略图", r);
    }
    void switchRenderer(std::shared_ptr<Renderer> r) {
        r_ = r;
        for (auto& v : views_) v.setRenderer(r);      // 最后一个引用放开时，旧设备自动释放
    }
    void draw(double t) { for (auto& v : views_) v.draw(t); }
    long users() const { return r_.use_count(); }
private:
    std::shared_ptr<Renderer> r_;
    std::vector<VideoView> views_;
};

int main() {
    PlayerWindow w(std::make_shared<MetalRenderer>());
    std::printf("后端被 %ld 处引用\n", w.users());
    w.draw(1.0);
    std::printf("-- 换成软件渲染 --\n");
    w.switchRenderer(std::make_shared<SoftRenderer>());
    w.draw(1.1);
}
