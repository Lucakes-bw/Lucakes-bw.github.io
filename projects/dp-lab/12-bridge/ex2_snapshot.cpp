// 《桥接模式》练习 2：给所有视图加「截图」。视图这边只改基类，后端这边每个都要实现「把画面读回来」
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Rect { int x, y, w, h; };
using Image = std::vector<std::string>;

class Renderer {
public:
    virtual ~Renderer() = default;
    virtual void begin(int w, int h) = 0;
    virtual void fill(Rect r, char c) = 0;
    virtual void present() = 0;
    virtual Image readback() = 0;                     // 新的基本操作：把刚画好的这一帧读回内存
};

class SoftRenderer : public Renderer {
public:
    void begin(int w, int h) override { rows_.assign(h, std::string(w, ' ')); }
    void fill(Rect r, char c) override {
        for (int y = r.y; y < r.y + r.h; ++y) rows_[y].replace(r.x, r.w, std::string(r.w, c));
    }
    void present() override {}
    Image readback() override { return rows_; }       // 画面本来就在内存里
private:
    Image rows_;
};

class SdlRenderer : public Renderer {
public:
    void begin(int w, int h) override { w_ = w; h_ = h; }
    void fill(Rect, char) override {}
    void present() override {}
    Image readback() override {                       // 真实代码：SDL_RenderReadPixels 把 GPU 上的画面拷回来
        std::printf("  SDL_RenderReadPixels(%d×%d)\n", w_, h_);
        return Image(h_, std::string(w_, '?'));
    }
private:
    int w_ = 0, h_ = 0;
};

class VideoView {
public:
    explicit VideoView(std::unique_ptr<Renderer> r) : r_(std::move(r)) {}
    virtual ~VideoView() = default;
    virtual void draw(double t) = 0;
    Image snapshot(double t) { draw(t); return r_->readback(); }   // 只在基类里写一次，所有视图都有了
protected:
    Renderer& r() { return *r_; }
private:
    std::unique_ptr<Renderer> r_;
};

class Thumbnail : public VideoView {
public:
    using VideoView::VideoView;
    void draw(double t) override {
        r().begin(8, 2);
        r().fill({0, 0, int(8 * t / 10), 2}, '#');
        r().present();
    }
};

int main() {
    Thumbnail soft(std::make_unique<SoftRenderer>()), sdl(std::make_unique<SdlRenderer>());
    for (auto* v : {&soft, &sdl})
        for (const auto& row : v->snapshot(5)) std::printf("  [%s]\n", row.c_str());
}
