// 《类型擦除》：经典 Concept/Model 写法——手写 Drawable 值类型包装器，非侵入式放入 std::vector
#include <cstdio>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// 三个互不继承、毫无关系的独立结构体（甚至可以是第三方库类型）
struct VideoLayer {
  int w, h;
  void draw() const { std::printf("  [Video] %dx%d\n", w, h); }
};

struct SubtitleLayer {
  std::string text;
  void draw() const { std::printf("  [Subtitle] \"%s\"\n", text.c_str()); }
};

struct Watermark {
  std::string brand;
  double alpha;
  void draw() const { std::printf("  [Watermark] %s (alpha=%.1f)\n", brand.c_str(), alpha); }
};

// 类型擦除值包装器：对外是普通值类型，对内隐藏 Concept / Model<T> 虚派发
class Drawable {
 public:
  template <typename T,
            typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, Drawable>>>
  Drawable(T x) : self_(std::make_unique<Model<std::decay_t<T>>>(std::move(x))) {}

  Drawable(const Drawable& other) : self_(other.self_->clone()) {}
  Drawable(Drawable&&) noexcept = default;
  Drawable& operator=(Drawable other) noexcept {  // copy-and-swap 统一拷贝与移动赋值
    self_.swap(other.self_);
    return *this;
  }

  void draw() const { self_->doDraw(); }

 private:
  struct Concept {
    virtual ~Concept() = default;
    virtual void doDraw() const = 0;
    virtual std::unique_ptr<const Concept> clone() const = 0;
  };

  template <typename T>
  struct Model final : Concept {
    T data_;
    explicit Model(T x) : data_(std::move(x)) {}
    void doDraw() const override { data_.draw(); }
    std::unique_ptr<const Concept> clone() const override {
      return std::make_unique<Model>(data_);
    }
  };

  std::unique_ptr<const Concept> self_;
};

int main() {
  std::vector<Drawable> scene;                    // 按值存储，无需 unique_ptr！
  scene.push_back(VideoLayer{1920, 1080});
  scene.push_back(SubtitleLayer{"Hello Type Erasure"});
  scene.push_back(Watermark{"LuckyStudio", 0.6});

  std::vector<Drawable> backup = scene;           // 天然支持值拷贝！
  scene.push_back(Watermark{"DRAFT", 0.3});

  std::printf("原场景 (%zu 层)：\n", scene.size());
  for (const auto& layer : scene) layer.draw();
  std::printf("快照场景 (%zu 层)：\n", backup.size());
  for (const auto& layer : backup) layer.draw();
}
