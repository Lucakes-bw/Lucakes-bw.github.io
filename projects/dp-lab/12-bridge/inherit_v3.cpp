// 《桥接模式》：v3：再加一种后端 Vulkan，继承版要加 4 个具体类
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

// ---------- 三个后端的代码（和桥接版一样，但它们没有共同的接口） ----------
class SoftCanvas {
public:
    void begin(int w, int h) { rows_.assign(h, std::string(w, ' ')); }
    void fill(Rect r, char c) { each(r, [&](int, int) { return c; }); }
    void frame(Rect r, double t) { each(r, [&](int x, int y) { return " .:-=+*%"[(x + y + int(t * 4)) % 8]; }); }
    void text(int x, int y, const std::string& s) { rows_[y].replace(x, s.size(), s); }
    void present() { for (const auto& row : rows_) std::printf("|%s|\n", row.c_str()); }
private:
    template <class F> void each(Rect r, F pixel) {
        for (int y = r.y; y < r.y + r.h; ++y)
            for (int x = r.x; x < r.x + r.w; ++x) rows_[y][x] = pixel(x, y);
    }
    std::vector<std::string> rows_;
};

class SdlApi {
public:
    void begin(int, int) { log_ = "SDL_RenderClear"; }
    void fill(Rect, char) { log_ += " RenderFillRect"; }
    void frame(Rect, double) { log_ += " UpdateTexture RenderTexture"; }
    void text(int, int, const std::string&) { log_ += " TTF_RenderText"; }
    void present() { std::printf("%s SDL_RenderPresent\n", log_.c_str()); }
private:
    std::string log_;
};

class MetalApi {
public:
    void begin(int, int) { log_ = "renderCommandEncoder"; }
    void fill(Rect, char) { log_ += " drawPrimitives"; }
    void frame(Rect, double) { log_ += " replaceRegion drawPrimitives"; }
    void text(int, int, const std::string&) { log_ += " drawGlyphs"; }
    void present() { std::printf("%s presentDrawable commit\n", log_.c_str()); }
private:
    std::string log_;
};

class VulkanApi {
public:
    void begin(int, int) { log_ = "vkCmdBeginRenderPass"; }
    void fill(Rect, char) { log_ += " vkCmdDraw"; }
    void frame(Rect, double) { log_ += " vkCmdCopyBufferToImage vkCmdDraw"; }
    void text(int, int, const std::string&) { log_ += " vkCmdDraw(glyphs)"; }
    void present() { std::printf("%s vkQueuePresentKHR\n", log_.c_str()); }
private:
    std::string log_;
};

// ---------- 视图：排版写在基类里，画图的基本操作留给子类 ----------
class VideoView {
public:
    virtual ~VideoView() = default;
    virtual std::string name() const = 0;
    virtual std::string backend() const = 0;
    virtual void draw(double t) = 0;
protected:
    virtual void begin(int w, int h) = 0;
    virtual void fill(Rect r, char c) = 0;
    virtual void frame(Rect r, double t) = 0;
    virtual void text(int x, int y, const std::string& s) = 0;
    virtual void present() = 0;
};

class MainWindow : public VideoView {
public:
    std::string name() const override { return "主窗口"; }
    void draw(double t) override {
        begin(30, 7);
        frame({0, 0, 30, 5}, t);
        fill({0, 5, 30, 1}, '-');
        fill({0, 5, int(30 * t / 10), 1}, '=');
        text(0, 6, clock(t) + " / 00:10");
        present();
    }
};

class Thumbnail : public VideoView {
public:
    std::string name() const override { return "缩略图"; }
    void draw(double t) override {
        begin(10, 4);
        frame({0, 0, 10, 3}, t);
        text(0, 3, clock(t));
        present();
    }
};

class PictureInPicture : public VideoView {
public:
    std::string name() const override { return "画中画"; }
    void moveTo(int x, int y) { x_ = x; y_ = y; }
    void draw(double t) override {
        begin(24, 5);
        fill({x_, y_, 10, 4}, '#');
        frame({x_ + 1, y_ + 1, 8, 2}, t);
        present();
    }
private:
    int x_ = 0, y_ = 1;
};

class MultiView : public VideoView {
public:
    std::string name() const override { return "多画面"; }
    void draw(double t) override {
        begin(21, 5);
        for (int i = 0; i < 4; ++i) frame({i % 2 * 11, i / 2 * 3, 10, 2}, t + i * 1.5);
        present();
    }
};

// ---------- 每种视图 × 每种后端，各写一个类 ----------
class MainWindowSoft : public MainWindow {
    SoftCanvas c_;
public:
    std::string backend() const override { return "软件"; }
protected:
    void begin(int w, int h) override { c_.begin(w, h); }
    void fill(Rect r, char c) override { c_.fill(r, c); }
    void frame(Rect r, double t) override { c_.frame(r, t); }
    void text(int x, int y, const std::string& s) override { c_.text(x, y, s); }
    void present() override { c_.present(); }
};

class MainWindowSdl : public MainWindow {
    SdlApi c_;
public:
    std::string backend() const override { return "SDL"; }
protected:
    void begin(int w, int h) override { c_.begin(w, h); }
    void fill(Rect r, char c) override { c_.fill(r, c); }
    void frame(Rect r, double t) override { c_.frame(r, t); }
    void text(int x, int y, const std::string& s) override { c_.text(x, y, s); }
    void present() override { c_.present(); }
};

class MainWindowMetal : public MainWindow {
    MetalApi c_;
public:
    std::string backend() const override { return "Metal"; }
protected:
    void begin(int w, int h) override { c_.begin(w, h); }
    void fill(Rect r, char c) override { c_.fill(r, c); }
    void frame(Rect r, double t) override { c_.frame(r, t); }
    void text(int x, int y, const std::string& s) override { c_.text(x, y, s); }
    void present() override { c_.present(); }
};

class ThumbnailSoft : public Thumbnail {
    SoftCanvas c_;
public:
    std::string backend() const override { return "软件"; }
protected:
    void begin(int w, int h) override { c_.begin(w, h); }
    void fill(Rect r, char c) override { c_.fill(r, c); }
    void frame(Rect r, double t) override { c_.frame(r, t); }
    void text(int x, int y, const std::string& s) override { c_.text(x, y, s); }
    void present() override { c_.present(); }
};

class ThumbnailSdl : public Thumbnail {
    SdlApi c_;
public:
    std::string backend() const override { return "SDL"; }
protected:
    void begin(int w, int h) override { c_.begin(w, h); }
    void fill(Rect r, char c) override { c_.fill(r, c); }
    void frame(Rect r, double t) override { c_.frame(r, t); }
    void text(int x, int y, const std::string& s) override { c_.text(x, y, s); }
    void present() override { c_.present(); }
};

class ThumbnailMetal : public Thumbnail {
    MetalApi c_;
public:
    std::string backend() const override { return "Metal"; }
protected:
    void begin(int w, int h) override { c_.begin(w, h); }
    void fill(Rect r, char c) override { c_.fill(r, c); }
    void frame(Rect r, double t) override { c_.frame(r, t); }
    void text(int x, int y, const std::string& s) override { c_.text(x, y, s); }
    void present() override { c_.present(); }
};

class PictureInPictureSoft : public PictureInPicture {
    SoftCanvas c_;
public:
    std::string backend() const override { return "软件"; }
protected:
    void begin(int w, int h) override { c_.begin(w, h); }
    void fill(Rect r, char c) override { c_.fill(r, c); }
    void frame(Rect r, double t) override { c_.frame(r, t); }
    void text(int x, int y, const std::string& s) override { c_.text(x, y, s); }
    void present() override { c_.present(); }
};

class PictureInPictureSdl : public PictureInPicture {
    SdlApi c_;
public:
    std::string backend() const override { return "SDL"; }
protected:
    void begin(int w, int h) override { c_.begin(w, h); }
    void fill(Rect r, char c) override { c_.fill(r, c); }
    void frame(Rect r, double t) override { c_.frame(r, t); }
    void text(int x, int y, const std::string& s) override { c_.text(x, y, s); }
    void present() override { c_.present(); }
};

class PictureInPictureMetal : public PictureInPicture {
    MetalApi c_;
public:
    std::string backend() const override { return "Metal"; }
protected:
    void begin(int w, int h) override { c_.begin(w, h); }
    void fill(Rect r, char c) override { c_.fill(r, c); }
    void frame(Rect r, double t) override { c_.frame(r, t); }
    void text(int x, int y, const std::string& s) override { c_.text(x, y, s); }
    void present() override { c_.present(); }
};

class MultiViewSoft : public MultiView {
    SoftCanvas c_;
public:
    std::string backend() const override { return "软件"; }
protected:
    void begin(int w, int h) override { c_.begin(w, h); }
    void fill(Rect r, char c) override { c_.fill(r, c); }
    void frame(Rect r, double t) override { c_.frame(r, t); }
    void text(int x, int y, const std::string& s) override { c_.text(x, y, s); }
    void present() override { c_.present(); }
};

class MultiViewSdl : public MultiView {
    SdlApi c_;
public:
    std::string backend() const override { return "SDL"; }
protected:
    void begin(int w, int h) override { c_.begin(w, h); }
    void fill(Rect r, char c) override { c_.fill(r, c); }
    void frame(Rect r, double t) override { c_.frame(r, t); }
    void text(int x, int y, const std::string& s) override { c_.text(x, y, s); }
    void present() override { c_.present(); }
};

class MultiViewMetal : public MultiView {
    MetalApi c_;
public:
    std::string backend() const override { return "Metal"; }
protected:
    void begin(int w, int h) override { c_.begin(w, h); }
    void fill(Rect r, char c) override { c_.fill(r, c); }
    void frame(Rect r, double t) override { c_.frame(r, t); }
    void text(int x, int y, const std::string& s) override { c_.text(x, y, s); }
    void present() override { c_.present(); }
};

class MainWindowVulkan : public MainWindow {
    VulkanApi c_;
public:
    std::string backend() const override { return "Vulkan"; }
protected:
    void begin(int w, int h) override { c_.begin(w, h); }
    void fill(Rect r, char c) override { c_.fill(r, c); }
    void frame(Rect r, double t) override { c_.frame(r, t); }
    void text(int x, int y, const std::string& s) override { c_.text(x, y, s); }
    void present() override { c_.present(); }
};

class ThumbnailVulkan : public Thumbnail {
    VulkanApi c_;
public:
    std::string backend() const override { return "Vulkan"; }
protected:
    void begin(int w, int h) override { c_.begin(w, h); }
    void fill(Rect r, char c) override { c_.fill(r, c); }
    void frame(Rect r, double t) override { c_.frame(r, t); }
    void text(int x, int y, const std::string& s) override { c_.text(x, y, s); }
    void present() override { c_.present(); }
};

class PictureInPictureVulkan : public PictureInPicture {
    VulkanApi c_;
public:
    std::string backend() const override { return "Vulkan"; }
protected:
    void begin(int w, int h) override { c_.begin(w, h); }
    void fill(Rect r, char c) override { c_.fill(r, c); }
    void frame(Rect r, double t) override { c_.frame(r, t); }
    void text(int x, int y, const std::string& s) override { c_.text(x, y, s); }
    void present() override { c_.present(); }
};

class MultiViewVulkan : public MultiView {
    VulkanApi c_;
public:
    std::string backend() const override { return "Vulkan"; }
protected:
    void begin(int w, int h) override { c_.begin(w, h); }
    void fill(Rect r, char c) override { c_.fill(r, c); }
    void frame(Rect r, double t) override { c_.frame(r, t); }
    void text(int x, int y, const std::string& s) override { c_.text(x, y, s); }
    void present() override { c_.present(); }
};

// 按配置创建：视图和后端的每一种组合都要列出来
std::unique_ptr<VideoView> makeView(const std::string& view, const std::string& backend) {
    if (view == "主窗口" && backend == "软件") return std::make_unique<MainWindowSoft>();
    if (view == "主窗口" && backend == "SDL") return std::make_unique<MainWindowSdl>();
    if (view == "主窗口" && backend == "Metal") return std::make_unique<MainWindowMetal>();
    if (view == "缩略图" && backend == "软件") return std::make_unique<ThumbnailSoft>();
    if (view == "缩略图" && backend == "SDL") return std::make_unique<ThumbnailSdl>();
    if (view == "缩略图" && backend == "Metal") return std::make_unique<ThumbnailMetal>();
    if (view == "画中画" && backend == "软件") return std::make_unique<PictureInPictureSoft>();
    if (view == "画中画" && backend == "SDL") return std::make_unique<PictureInPictureSdl>();
    if (view == "画中画" && backend == "Metal") return std::make_unique<PictureInPictureMetal>();
    if (view == "多画面" && backend == "软件") return std::make_unique<MultiViewSoft>();
    if (view == "多画面" && backend == "SDL") return std::make_unique<MultiViewSdl>();
    if (view == "多画面" && backend == "Metal") return std::make_unique<MultiViewMetal>();
    if (view == "主窗口" && backend == "Vulkan") return std::make_unique<MainWindowVulkan>();
    if (view == "缩略图" && backend == "Vulkan") return std::make_unique<ThumbnailVulkan>();
    if (view == "画中画" && backend == "Vulkan") return std::make_unique<PictureInPictureVulkan>();
    if (view == "多画面" && backend == "Vulkan") return std::make_unique<MultiViewVulkan>();
    return nullptr;
}

int main() {
    auto main = makeView("主窗口", "软件");
    auto thumb = makeView("缩略图", "Vulkan");
    auto pip = makeView("画中画", "Metal");
    static_cast<PictureInPicture&>(*pip).moveTo(14, 0);    // 用户把画中画拖到了右上角

    auto multi = makeView("多画面", "软件");
    for (auto* v : {main.get(), thumb.get(), pip.get(), multi.get()}) {
        std::printf("== %s（%s）\n", v->name().c_str(), v->backend().c_str());
        v->draw(3.5);
    }
    std::printf("-- 画中画拖到外接显示器上，Metal 不可用，换成软件渲染 --\n");
    pip = makeView("画中画", "软件");                        // 后端是类型的一部分，只能换一个新对象
    std::printf("== %s（%s）\n", pip->name().c_str(), pip->backend().c_str());
    pip->draw(3.75);
}
