// 《桥接模式》：视图（主窗口、缩略图、画中画）× 渲染后端（SDL、Metal、软件），桥接只要 3 + 3 个类
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct Rect { int x, y, w, h; };

std::string clock(double t) {                       // 3.5 → "00:03"
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d:%02d", int(t) / 60, int(t) % 60);
    return buf;
}

// ---------- 实现部分：渲染后端只提供几个基本操作 ----------
class Renderer {
public:
    virtual ~Renderer() = default;
    virtual std::string name() const = 0;
    virtual void begin(int w, int h) = 0;                       // 开始画一帧，画布 w × h
    virtual void fill(Rect r, char c) = 0;                      // 填充矩形（边框、进度条）
    virtual void frame(Rect r, double t) = 0;                   // 把 t 秒处的视频帧缩放画进 r
    virtual void text(int x, int y, const std::string& s) = 0;  // 写一行字
    virtual void present() = 0;                                 // 把这一帧显示出来
};

// 软件渲染：画在内存里的字符画布上，present 时打印出来
class SoftRenderer : public Renderer {
public:
    std::string name() const override { return "软件"; }
    void begin(int w, int h) override { rows_.assign(h, std::string(w, ' ')); }
    void fill(Rect r, char c) override { each(r, [&](int, int) { return c; }); }
    void frame(Rect r, double t) override {                     // 用移动的明暗条纹代替画面
        each(r, [&](int x, int y) { return " .:-=+*%"[(x + y + int(t * 4)) % 8]; });
    }
    void text(int x, int y, const std::string& s) override { rows_[y].replace(x, s.size(), s); }
    void present() override { for (const auto& row : rows_) std::printf("|%s|\n", row.c_str()); }
private:
    template <class F> void each(Rect r, F pixel) {
        for (int y = r.y; y < r.y + r.h; ++y)
            for (int x = r.x; x < r.x + r.w; ++x) rows_[y][x] = pixel(x, y);
    }
    std::vector<std::string> rows_;
};

// SDL 和 Metal：这里只记下会调用哪些 API，present 时打印一行
class SdlRenderer : public Renderer {
public:
    std::string name() const override { return "SDL"; }
    void begin(int, int) override { log_ = "SDL_RenderClear"; }
    void fill(Rect, char) override { log_ += " RenderFillRect"; }
    void frame(Rect, double) override { log_ += " UpdateTexture RenderTexture"; }
    void text(int, int, const std::string&) override { log_ += " TTF_RenderText"; }
    void present() override { std::printf("%s SDL_RenderPresent\n", log_.c_str()); }
private:
    std::string log_;
};

class MetalRenderer : public Renderer {
public:
    std::string name() const override { return "Metal"; }
    void begin(int, int) override { log_ = "renderCommandEncoder"; }
    void fill(Rect, char) override { log_ += " drawPrimitives"; }
    void frame(Rect, double) override { log_ += " replaceRegion drawPrimitives"; }
    void text(int, int, const std::string&) override { log_ += " drawGlyphs"; }
    void present() override { std::printf("%s presentDrawable commit\n", log_.c_str()); }
private:
    std::string log_;
};

// ---------- 抽象部分：视图决定画什么、怎么排版，通过 Renderer 去画 ----------
class VideoView {
public:
    explicit VideoView(std::unique_ptr<Renderer> r) : r_(std::move(r)) {}
    virtual ~VideoView() = default;
    virtual std::string name() const = 0;
    virtual void draw(double t) = 0;
    void setRenderer(std::unique_ptr<Renderer> r) { r_ = std::move(r); }   // 运行时换后端，视图的状态不变
    std::string backend() const { return r_->name(); }
protected:
    Renderer& r() { return *r_; }
private:
    std::unique_ptr<Renderer> r_;                                          // 桥：视图只通过它使用后端
};

class MainWindow : public VideoView {                // 画面 + 进度条 + 时间
public:
    using VideoView::VideoView;
    std::string name() const override { return "主窗口"; }
    void draw(double t) override {
        r().begin(30, 7);
        r().frame({0, 0, 30, 5}, t);
        r().fill({0, 5, 30, 1}, '-');
        r().fill({0, 5, int(30 * t / 10), 1}, '=');  // 测试视频 10 秒
        r().text(0, 6, clock(t) + " / 00:10");
        r().present();
    }
};

class Thumbnail : public VideoView {                 // 小图 + 时间戳
public:
    using VideoView::VideoView;
    std::string name() const override { return "缩略图"; }
    void draw(double t) override {
        r().begin(10, 4);
        r().frame({0, 0, 10, 3}, t);
        r().text(0, 3, clock(t));
        r().present();
    }
};

class PictureInPicture : public VideoView {          // 屏幕上一个带边框的小窗，可以拖动
public:
    using VideoView::VideoView;
    std::string name() const override { return "画中画"; }
    void moveTo(int x, int y) { x_ = x; y_ = y; }
    void draw(double t) override {
        r().begin(24, 5);                            // 整个屏幕
        r().fill({x_, y_, 10, 4}, '#');              // 边框
        r().frame({x_ + 1, y_ + 1, 8, 2}, t);
        r().present();
    }
private:
    int x_ = 0, y_ = 1;                              // 默认在左下角
};

// 按配置创建后端：每种后端一行；视图直接构造，把后端传进去
std::unique_ptr<Renderer> makeRenderer(const std::string& name) {
    if (name == "软件") return std::make_unique<SoftRenderer>();
    if (name == "SDL") return std::make_unique<SdlRenderer>();
    if (name == "Metal") return std::make_unique<MetalRenderer>();
    return nullptr;
}

int main() {
    MainWindow main(makeRenderer("软件"));
    Thumbnail thumb(makeRenderer("SDL"));
    PictureInPicture pip(makeRenderer("Metal"));
    pip.moveTo(14, 0);                               // 用户把画中画拖到了右上角

    std::vector<VideoView*> views = {&main, &thumb, &pip};
    for (auto* v : views) {
        std::printf("== %s（%s）\n", v->name().c_str(), v->backend().c_str());
        v->draw(3.5);
    }
    std::printf("-- 画中画拖到外接显示器上，Metal 不可用，换成软件渲染 --\n");
    pip.setRenderer(makeRenderer("软件"));                   // 同一个对象，换一个后端
    std::printf("== %s（%s）\n", pip.name().c_str(), pip.backend().c_str());
    pip.draw(3.75);
}
