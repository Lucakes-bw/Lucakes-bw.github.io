// 《类型擦除》：问题版——传统虚基类要求侵入式继承、第三方类被迫写适配器，且容器丢失值语义
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct IDrawable {
  virtual ~IDrawable() = default;
  virtual void draw() const = 0;
  virtual std::unique_ptr<IDrawable> clone() const = 0;  // 痛点 ①：想拷贝场景，每个子类都得手写 clone()
};

// 痛点 ②：自研类被迫侵入式继承 IDrawable
struct VideoLayer : IDrawable {
  int w, h;
  VideoLayer(int w, int h) : w(w), h(h) {}
  void draw() const override { std::printf("  [Video] %dx%d\n", w, h); }
  std::unique_ptr<IDrawable> clone() const override { return std::make_unique<VideoLayer>(*this); }
};

struct SubtitleLayer : IDrawable {
  std::string text;
  explicit SubtitleLayer(std::string t) : text(std::move(t)) {}
  void draw() const override { std::printf("  [Subtitle] \"%s\"\n", text.c_str()); }
  std::unique_ptr<IDrawable> clone() const override { return std::make_unique<SubtitleLayer>(*this); }
};

// 第三方 SDK 的水印类：无法修改源码，不能让它继承 IDrawable
struct SdkWatermark {
  std::string brand;
  double alpha;
  void render() const { std::printf("  [Watermark] %s (alpha=%.1f)\n", brand.c_str(), alpha); }
};

// 痛点 ③：每个第三方类都要额外写一个适配器包装类
struct WatermarkAdapter : IDrawable {
  SdkWatermark wm;
  explicit WatermarkAdapter(SdkWatermark w) : wm(std::move(w)) {}
  void draw() const override { wm.render(); }
  std::unique_ptr<IDrawable> clone() const override { return std::make_unique<WatermarkAdapter>(*this); }
};

using Scene = std::vector<std::unique_ptr<IDrawable>>;   // 痛点 ④：被迫装指针，不能直接拷贝

Scene cloneScene(const Scene& src) {
  Scene dst;
  dst.reserve(src.size());
  for (const auto& item : src) dst.push_back(item->clone());
  return dst;
}

int main() {
  Scene scene;
  scene.push_back(std::make_unique<VideoLayer>(1920, 1080));
  scene.push_back(std::make_unique<SubtitleLayer>("Hello Type Erasure"));
  scene.push_back(std::make_unique<WatermarkAdapter>(SdkWatermark{"LuckyStudio", 0.6}));

  // Scene backup = scene;  // 编译失败：std::unique_ptr 不可拷贝！
  Scene backup = cloneScene(scene);
  std::printf("原场景 (%zu 层)：\n", scene.size());
  for (const auto& layer : scene) layer->draw();
  std::printf("克隆场景 (%zu 层)：\n", backup.size());
  for (const auto& layer : backup) layer->draw();
}
