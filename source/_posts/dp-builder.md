---
title: 建造者模式
date: 2026-11-08 17:00:00
updated: 2026-11-08 17:00:00
categories:
  - 设计模式
  - 创建型模式
tags:
  - 设计模式
  - 建造者
  - C++20
  - FFmpeg
order: 4
description: 一个转码任务有输入、输出、编码器、CRF、码率、预设、分辨率、音频八个参数。写成 8 个参数的构造函数，把宽和高写反了编译器一声不吭，ffmpeg 照样执行，得到一个 360×640 的竖屏视频；宽度写成 641，libx264 直接报错。建造者用带名字的链式调用一步步设置参数，在 build() 里统一检查，错误在执行 ffmpeg 之前就被拦下。再对比 GoF 原版的指挥者、C++20 的指定初始化（Clang 和 GCC 对顺序的处理居然不同），以及在编译期保证必填项的类型状态建造者。
top_group_list: true
disableNunjucks: true
viz: [dp-builder]
---

> **本文目标**：知道参数很多的对象该怎么创建；会写带检查的链式建造者，理解 GoF 原版的「指挥者 + 建造者」；会用 C++20 的指定初始化、强类型参数和类型状态建造者，并知道它们各自的适用范围。
>
> **前置知识**：上一篇《抽象工厂》；C/C++ 专栏的《std::optional：可能没有值的类型》《右值引用与移动语义》；最好读过音视频专栏的《FFmpeg 的库与开发环境》，知道 `ffmpeg` 命令行的基本用法。
>
> **编译环境**：`g++ -std=c++17` / `clang++ -std=c++17`，第 4.1 节的程序要用 `-std=c++20`。完整代码在仓库的 `projects/dp-lab/09-builder/` 目录，真正执行转码的程序在 `encode/` 子目录。

## 1. 问题：参数太多的对象

### 1.1 八个参数的构造函数

一个视频转码任务需要不少参数：输入文件、输出文件、编码器、质量（CRF）、编码速度预设、输出分辨率、要不要音频……最直接的写法是一个构造函数全部接收：

```cpp
#include <iostream>
#include <string>

class EncodeJob {
public:
    EncodeJob(std::string input, std::string output, std::string codec, int crf,
              std::string preset, int width, int height, bool audio)
        : input_(std::move(input)), output_(std::move(output)), codec_(std::move(codec)), crf_(crf),
          preset_(std::move(preset)), width_(width), height_(height), audio_(audio) {}

    std::string commandLine() const {
        std::string cmd = "ffmpeg -i " + input_ + " -c:v " + codec_ + " -crf " + std::to_string(crf_) +
                          " -preset " + preset_ + " -vf scale=" + std::to_string(width_) + ":" + std::to_string(height_);
        cmd += audio_ ? " -c:a aac" : " -an";
        return cmd + " " + output_;
    }

private:
    std::string input_, output_, codec_;
    int crf_;
    std::string preset_;
    int width_, height_;
    bool audio_;
};

int main() {
    // 想要 640×360 的预览版。你能一眼看出哪个数是宽、哪个是高，true 又是什么意思吗？
    EncodeJob job("bbb-10s.mp4", "preview.mp4", "libx264", 28, "veryfast", 360, 640, true);
    std::cout << job.commandLine() << '\n';
}
```

```text
ffmpeg -i bbb-10s.mp4 -c:v libx264 -crf 28 -preset veryfast -vf scale=360:640 -c:a aac preview.mp4
```

想要 640×360 的预览版，调用时却把宽和高写反了。两个参数都是 `int`，编译器无从发现；最后那个 `true` 是什么意思，不翻到构造函数的声明也看不出来。

这样的命令会怎样？`encode/run_encode.cpp` 把它真的交给 ffmpeg 执行，转码专栏的测试视频 `bbb-10s.mp4`：

```text
== 构造函数参数写反
   ffmpeg -y -loglevel error -i bbb-10s.mp4 -c:v libx264 -crf 28 -preset veryfast -vf scale=360:640 -c:a aac swapped.mp4
   → 360x640，508 KB，用时 0.20 s
```

ffmpeg 没有报任何错误，老老实实地输出了一个 360×640 的竖屏视频。截取第 4 秒的画面（左边是正确的 640×360，右边是写反后的结果）：

<img src="/img/dp/builder-swap-compare.jpg" alt="左边是 640×360 的正常画面；右边是宽高写反后的 360×640 画面，兔子和花被横向挤压、纵向拉长" width="680">

再换一种错误：宽度写成了奇数 641。这一次 ffmpeg 倒是报错了：

```text
== 没有检查：宽度 641
   ffmpeg -y -loglevel error -i bbb-10s.mp4 -c:v libx264 -crf 28 -preset veryfast -vf scale=641:360 -c:a aac odd.mp4
[libx264 @ 0x7818c29180] width not divisible by 2 (641x360)
   → 没有生成输出文件
```

libx264 要求宽和高都是偶数（H.264 的色度采样是 4:2:0，宽高都要能被 2 整除）。可这个错误要等到真正开始转码时才发现：如果它在一个排了几个小时的任务队列里，就要等到轮到它的时候才会失败。

### 1.2 其他常见的写法也不理想

- **重叠构造函数**（telescoping constructors）：写一串参数个数递增的重载，`EncodeJob(in, out)`、`EncodeJob(in, out, crf)`、`EncodeJob(in, out, crf, preset)`……参数稍多一点，重载就多得写不完，而且想跳过中间某个参数也做不到。
- **先构造、再调用 setter**：`EncodeJob job; job.setInput(...); job.setCrf(...);`。可读性好了，但对象在设置完之前一直处于「半成品」状态，忘了设置必填项、设置了互相冲突的参数，都要等到使用时才暴露；对象也不能再是不可变的。

## 2. 建造者

### 2.1 意图

**建造者模式**（Builder）：将一个复杂对象的构建和它的表示分离，使得同样的构建过程可以创建不同的表示。

GoF 的这个定义说的是第 3 节的原版写法。今天大家说「建造者」，更多指的是一种更朴素的用法：**用一个专门的建造者对象，一步步收集参数，最后一次性检查并创建产品**。

### 2.2 链式建造者

```cpp
#include <iostream>
#include <optional>
#include <string>

struct EncodeJob {                                    // 产品：一旦建好就不再修改
    std::string input, output, codec, preset;
    std::optional<int> crf, bitrateK;
    int width = 0, height = 0;                        // 0 表示保持原尺寸
    bool audio = true;

    std::string commandLine() const {
        std::string cmd = "ffmpeg -i " + input + " -c:v " + codec;
        if (crf) cmd += " -crf " + std::to_string(*crf);
        if (bitrateK) cmd += " -b:v " + std::to_string(*bitrateK) + "k";
        cmd += " -preset " + preset;
        if (width) cmd += " -vf scale=" + std::to_string(width) + ":" + std::to_string(height);
        cmd += audio ? " -c:a aac" : " -an";
        return cmd + " " + output;
    }
};

struct BuildResult {                                  // 要么得到任务，要么得到错误原因
    std::optional<EncodeJob> job;
    std::string error;
};

class EncodeJobBuilder {
public:
    // 每个设置函数都返回 *this 的引用，所以可以一路点下去
    EncodeJobBuilder& input(std::string path) { job_.input = std::move(path); return *this; }
    EncodeJobBuilder& output(std::string path) { job_.output = std::move(path); return *this; }
    EncodeJobBuilder& codec(std::string c) { job_.codec = std::move(c); return *this; }
    EncodeJobBuilder& preset(std::string p) { job_.preset = std::move(p); return *this; }
    EncodeJobBuilder& crf(int q) { job_.crf = q; return *this; }
    EncodeJobBuilder& bitrate(int kbps) { job_.bitrateK = kbps; return *this; }
    EncodeJobBuilder& size(int width, int height) { job_.width = width; job_.height = height; return *this; }
    EncodeJobBuilder& noAudio() { job_.audio = false; return *this; }

    // 所有检查集中在这里：要么返回一个合法的任务，要么说明哪里不对
    BuildResult build() const {
        if (job_.input.empty()) return {std::nullopt, "缺少输入文件"};
        if (job_.output.empty()) return {std::nullopt, "缺少输出文件"};
        if (job_.crf && job_.bitrateK) return {std::nullopt, "crf 和码率只能二选一"};
        if (job_.crf && (*job_.crf < 0 || *job_.crf > 51)) return {std::nullopt, "crf 必须在 0～51 之间"};
        if (job_.width % 2 || job_.height % 2) return {std::nullopt, "libx264 要求宽和高都是偶数"};
        return {job_, ""};
    }

private:
    EncodeJob job_{"", "", "libx264", "medium", std::nullopt, std::nullopt, 0, 0, true};   // 默认值集中在一处
};

void show(const BuildResult& r) {
    if (r.job) std::cout << "✓ " << r.job->commandLine() << '\n';
    else std::cout << "✗ " << r.error << '\n';
}

int main() {
    show(EncodeJobBuilder().input("bbb-10s.mp4").output("preview.mp4")
             .crf(28).preset("veryfast").size(640, 360).build());           // 每个参数都带着名字
    show(EncodeJobBuilder().input("bbb-10s.mp4").output("archive.mp4").build());   // 其余用默认值
    show(EncodeJobBuilder().input("bbb-10s.mp4").crf(28).build());          // 忘了输出
    show(EncodeJobBuilder().input("bbb-10s.mp4").output("x.mp4").crf(28).bitrate(800).build());
    show(EncodeJobBuilder().input("bbb-10s.mp4").output("x.mp4").size(641, 360).build());
}
```

```text
✓ ffmpeg -i bbb-10s.mp4 -c:v libx264 -crf 28 -preset veryfast -vf scale=640:360 -c:a aac preview.mp4
✓ ffmpeg -i bbb-10s.mp4 -c:v libx264 -preset medium -c:a aac archive.mp4
✗ 缺少输出文件
✗ crf 和码率只能二选一
✗ libx264 要求宽和高都是偶数
```

和 1.1 节的构造函数相比：

- **每个参数都带着名字**：`.size(640, 360)` 比构造函数里的第 6、7 个参数清楚得多（宽高写反的风险还在，4.2 节会彻底解决它）；
- **不用的参数可以不写**，默认值集中在建造者里一处；
- **检查集中在 `build()` 里**：必填项、取值范围、参数之间的冲突，都在创建产品的那一刻检查，不合法就拿不到产品，只拿到错误原因；
- **产品是完整、合法的**：`EncodeJob` 一旦建好就不再修改，使用它的代码不必再担心它是半成品。

每个设置函数都返回 `*this` 的引用，所以调用可以一路「点」下去，这种写法叫**流式接口**（fluent interface）。下面的演示展示了链式调用一步步填写草稿、最后 `build()` 逐条检查的过程，可以切换四种情况：

<div class="dsviz" data-viz="dp-builder"></div>

### 2.3 实测：把生成的命令交给 ffmpeg

`encode/run_encode.cpp` 用同一个建造者生成两个质量不同的预览版，真正执行转码：

```text
== 建造者：预览版 crf 28
   ffmpeg -y -loglevel error -i bbb-10s.mp4 -c:v libx264 -crf 28 -preset veryfast -vf scale=640:360 -c:a aac preview_crf28.mp4
   → 640x360，527 KB，用时 0.27 s
== 建造者：预览版 crf 23
   ffmpeg -y -loglevel error -i bbb-10s.mp4 -c:v libx264 -crf 23 -preset veryfast -vf scale=640:360 -c:a aac preview_crf23.mp4
   → 640x360，853 KB，用时 0.20 s
== 没有检查：宽度 641
   ……
   建造者在执行之前就拒绝了它：libx264 要求宽和高都是偶数
```

CRF 越小画质越高、文件越大：同样 10 秒的 640×360，CRF 23 是 853 KB，CRF 28 是 527 KB（音视频专栏的《编码》讲过 CRF 的含义）。宽度 641 那一项，没有检查的代码要真的调用 ffmpeg 才知道失败，建造者在生成命令时就拒绝了它。

## 3. GoF 原版：指挥者和多种表示

GoF 的建造者多了一个角色：**指挥者**（Director）。指挥者知道「一个产品由哪些步骤组成」，但不知道每一步具体产出什么；建造者负责实现每一步。换一个建造者，同样的步骤就得到另一种表示：

```cpp
#include <iostream>
#include <string>

// 建造者接口：转码任务的各个组成部分
class EncodeSpecBuilder {
public:
    virtual ~EncodeSpecBuilder() = default;
    virtual void source(const std::string& path) = 0;
    virtual void video(const std::string& codec, int crf, const std::string& preset) = 0;
    virtual void scale(int width, int height) = 0;
    virtual void audio(int kbps) = 0;                  // 0 表示去掉音频
    virtual void target(const std::string& path) = 0;
};

// 表示一：可以直接执行的 ffmpeg 命令行
class CommandLineBuilder : public EncodeSpecBuilder {
public:
    void source(const std::string& p) override { in_ = " -i " + p; }
    void video(const std::string& c, int crf, const std::string& preset) override {
        v_ = " -c:v " + c + " -crf " + std::to_string(crf) + " -preset " + preset;
    }
    void scale(int w, int h) override { v_ += " -vf scale=" + std::to_string(w) + ":" + std::to_string(h); }
    void audio(int kbps) override { a_ = kbps ? " -c:a aac -b:a " + std::to_string(kbps) + "k" : " -an"; }
    void target(const std::string& p) override { out_ = " " + p; }
    std::string result() const { return "ffmpeg" + in_ + v_ + a_ + out_; }
private:
    std::string in_, v_, a_, out_;
};

// 表示二：交给任务队列的 JSON
class JsonBuilder : public EncodeSpecBuilder {
public:
    void source(const std::string& p) override { add("input", "\"" + p + "\""); }
    void video(const std::string& c, int crf, const std::string& preset) override {
        add("codec", "\"" + c + "\""); add("crf", std::to_string(crf)); add("preset", "\"" + preset + "\"");
    }
    void scale(int w, int h) override { add("size", "[" + std::to_string(w) + ", " + std::to_string(h) + "]"); }
    void audio(int kbps) override { add("audio_kbps", std::to_string(kbps)); }
    void target(const std::string& p) override { add("output", "\"" + p + "\""); }
    std::string result() const { return "{" + body_ + "}"; }
private:
    void add(const std::string& k, const std::string& v) { body_ += (body_.empty() ? "" : ", ") + ("\"" + k + "\": " + v); }
    std::string body_;
};

// 指挥者：知道「手机预览版」「存档版」分别由哪些步骤组成，但不关心产出的是什么表示
class Director {
public:
    void mobilePreview(EncodeSpecBuilder& b, const std::string& in, const std::string& out) {
        b.source(in);
        b.video("libx264", 28, "veryfast");
        b.scale(640, 360);
        b.audio(64);
        b.target(out);
    }
    void archive(EncodeSpecBuilder& b, const std::string& in, const std::string& out) {
        b.source(in);
        b.video("libx264", 18, "slow");                // 不缩放，高画质
        b.audio(192);
        b.target(out);
    }
};

int main() {
    Director director;
    CommandLineBuilder cmd;
    JsonBuilder json;
    director.mobilePreview(cmd, "bbb-10s.mp4", "preview.mp4");
    director.mobilePreview(json, "bbb-10s.mp4", "preview.mp4");
    std::cout << cmd.result() << '\n' << json.result() << '\n';

    CommandLineBuilder cmd2;
    director.archive(cmd2, "bbb-10s.mp4", "archive.mp4");
    std::cout << cmd2.result() << '\n';
}
```

```text
ffmpeg -i bbb-10s.mp4 -c:v libx264 -crf 28 -preset veryfast -vf scale=640:360 -c:a aac -b:a 64k preview.mp4
{"input": "bbb-10s.mp4", "codec": "libx264", "crf": 28, "preset": "veryfast", "size": [640, 360], "audio_kbps": 64, "output": "preview.mp4"}
ffmpeg -i bbb-10s.mp4 -c:v libx264 -crf 18 -preset slow -c:a aac -b:a 192k archive.mp4
```

```uml
label: GoF 建造者：Director 按固定步骤调用 EncodeSpecBuilder 接口；CommandLineBuilder 产出命令行，JsonBuilder 产出 JSON
classes:
  - { id: D, name: Director, x: 20, y: 48.5, w: 230, ops: ['+ mobilePreview(b, in, out)', '+ archive(b, in, out)'] }
  - { id: B, name: EncodeSpecBuilder, stereo: interface, x: 330, y: 16, w: 250, ops: ['*+ source(path)', '*+ video(codec, crf, preset)', '*+ scale(w, h)', '*+ audio(kbps)', '*+ target(path)'] }
  - { id: C, name: CommandLineBuilder, x: 210, y: 230, w: 200, ops: ['+ result() : string  // 命令行'] }
  - { id: J, name: JsonBuilder, x: 490, y: 230, w: 200, ops: ['+ result() : string  // JSON'] }
rels:
  - { from: D, to: B, type: depend, label: '按步骤调用' }
  - { from: C, to: B, type: realize, mid: 190 }
  - { from: J, to: B, type: realize, mid: 190 }
```

这里有两点值得注意：

- 「手机预览版由哪几步组成」这个知识只写在 `Director` 里，命令行和 JSON 两种表示共用它；新增一种表示（比如生成一个 Windows 批处理文件），只需新写一个建造者。
- 产品不是从建造者接口里返回的，而是由每个具体建造者自己的 `result()` 提供，因为不同表示的产品类型可以完全不同。

GoF 原版在需要「同一套步骤、多种输出」时才用得上，比如文档转换（同一份文档的结构，输出成 HTML、Markdown、PDF）、编译器的代码生成。日常写代码遇到的大多是第 2 节的情况：只有一种产品，只是参数多。

## 4. C++ 里的其他写法

### 4.1 C++20 的指定初始化

如果参数之间没有需要检查的约束，C++20 的**指定初始化**（designated initializers）让一个普通结构体就能做到「每个参数带名字、没写的用默认值」：

<!-- run: flags="-std=c++20" -->
```cpp
#include <iostream>
#include <string>

struct EncodeOptions {
    std::string input;
    std::string output;
    std::string codec = "libx264";
    int crf = 23;
    std::string preset = "medium";
    int width = 0, height = 0;
    bool audio = true;
};

std::string commandLine(const EncodeOptions& o) {
    std::string cmd = "ffmpeg -i " + o.input + " -c:v " + o.codec + " -crf " + std::to_string(o.crf) + " -preset " + o.preset;
    if (o.width) cmd += " -vf scale=" + std::to_string(o.width) + ":" + std::to_string(o.height);
    return cmd + (o.audio ? " -c:a aac " : " -an ") + o.output;
}

int main() {
    // 每个值都带着名字，没写的成员用默认值
    std::cout << commandLine({.input = "bbb-10s.mp4", .output = "preview.mp4", .crf = 28, .width = 640, .height = 360}) << '\n';
    std::cout << commandLine({.input = "bbb-10s.mp4", .output = "silent.mp4", .audio = false}) << '\n';
}
```

```text
ffmpeg -i bbb-10s.mp4 -c:v libx264 -crf 28 -preset medium -vf scale=640:360 -c:a aac preview.mp4
ffmpeg -i bbb-10s.mp4 -c:v libx264 -crf 23 -preset medium -an silent.mp4
```

它有几个限制：

- **必须按成员声明的顺序写**。把 `.width` 写在 `.input` 前面（`designated_order_ce.cpp`），GCC 13 直接报错 `designator order for field 'EncodeOptions::input' does not match declaration order`；有意思的是，Clang 21 只给了一条警告 `ISO C++ requires field designators to be specified in declaration order`，照样编译通过。同一段代码在两个编译器上的结果不同，所以不要依赖 Clang 的宽容。
- **没有地方做检查**：必填项可以不写，冲突的参数可以同时写，结构体都照单全收。
- 结构体必须是**聚合类型**（没有用户声明的构造函数、没有私有成员），所以产品本身也没法保护自己的不变式。

适合参数很多、但都是独立的可选项、不需要检查的场合，比如函数的配置参数。

### 4.2 强类型参数：让宽和高不可能写反

1.1 节的宽高写反，根源在于两个参数都是 `int`。给它们各自一个类型，写反了就是编译错误：

<!-- run: off -->
```cpp
struct Width { int value; };
struct Height { int value; };

EncodeJobBuilder& size(Width w, Height h);

builder.size(Width{640}, Height{360});     // 正确
builder.size(Height{360}, Width{640});     // 编译错误：类型不匹配
builder.size(640, 360);                    // 编译错误：必须写明是宽还是高
```

这个技巧和建造者是正交的，可以在任何地方用：凡是相邻的几个参数类型相同、又容易搞混的（宽和高、起点和终点、毫秒和秒），都值得考虑。

### 4.3 类型状态建造者：在编译期保证必填项

2.2 节的建造者要到运行时才能发现「忘了设置输出」。C++ 的模板可以把检查提前到编译期：用模板参数记录「输入设置了没有」「输出设置了没有」，每设置一项就返回另一个类型的建造者，只有两项都设置了，`build()` 才能通过编译：

```cpp
#include <iostream>
#include <string>

struct EncodeJob {
    std::string input, output;
    int crf = 23;
    std::string commandLine() const { return "ffmpeg -i " + input + " -c:v libx264 -crf " + std::to_string(crf) + " " + output; }
};

// 两个模板参数记录「输入设置了没有」「输出设置了没有」，每设置一项，返回的就是另一个类型
template <bool HasInput, bool HasOutput>
class JobBuilder {
public:
    JobBuilder() = default;
    explicit JobBuilder(EncodeJob j) : job_(std::move(j)) {}

    JobBuilder<true, HasOutput> input(std::string p) && { job_.input = std::move(p); return JobBuilder<true, HasOutput>(std::move(job_)); }
    JobBuilder<HasInput, true> output(std::string p) && { job_.output = std::move(p); return JobBuilder<HasInput, true>(std::move(job_)); }
    JobBuilder crf(int q) && { job_.crf = q; return std::move(*this); }

    EncodeJob build() && {
        static_assert(HasInput, "必须先调用 input()");
        static_assert(HasOutput, "必须先调用 output()");
        return std::move(job_);
    }

private:
    EncodeJob job_;
};

using EncodeJobBuilder = JobBuilder<false, false>;

int main() {
    EncodeJob job = EncodeJobBuilder().input("bbb-10s.mp4").crf(28).output("preview.mp4").build();
    std::cout << job.commandLine() << '\n';
    // EncodeJobBuilder().input("bbb-10s.mp4").build();   // 编译错误：必须先调用 output()
}
```

```text
ffmpeg -i bbb-10s.mp4 -c:v libx264 -crf 28 preview.mp4
```

忘了调用 `output()` 时（`typestate_ce.cpp`），Clang 和 GCC 都报出同样的错误：

```text
error: static assertion failed: 必须先调用 output()
```

设置函数和 `build()` 后面的 `&&` 表示它们只能在右值上调用，也就是只能接在 `EncodeJobBuilder()` 这样的临时对象后面一路点下去，避免同一个建造者被重复使用（练习 4）。这种写法的代价是代码复杂、编译报错难读，只值得用在「忘了设置会造成严重后果」的地方，比如库的公开接口。

### 4.4 怎么选

| 写法 | 参数带名字 | 可以有默认值 | 检查参数之间的约束 | 必填项检查 | 适合 |
| --- | --- | --- | --- | --- | --- |
| 多参数构造函数 | ✗ | 只能放在末尾 | ✓（构造函数里） | ✓ | 参数少（三个以内）且类型各不相同 |
| C++20 指定初始化 | ✓ | ✓ | ✗ | ✗ | 一堆独立的可选配置 |
| 链式建造者 | ✓ | ✓ | ✓（运行时） | ✓（运行时） | 参数多、有约束，最常用 |
| 类型状态建造者 | ✓ | ✓ | ✓ | ✓（编译期） | 公开接口，必填项绝不能漏 |
| GoF 指挥者 + 建造者 | — | — | — | — | 同样的步骤要产出多种表示 |

## 5. 几个实现细节

- **设置函数返回引用**（`EncodeJobBuilder&`），才能连续调用；返回值的话，每一步都会复制一份草稿。
- **默认值集中在建造者里**，产品类本身可以没有默认值，也可以把构造函数设为私有、只让建造者创建它。
- **错误怎么报告**：本文用一个「任务或错误原因」的结构体，这样在不支持异常的环境里（比如本博客的浏览器内运行）也能用。C++23 的 `std::expected<EncodeJob, std::string>` 正是为此设计的；也可以在 `build()` 里直接抛异常。
- **建造者通常只用一次**：`build()` 之后建造者的状态就没用了。需要基于一个模板反复创建相似的产品时，可以先建好一个建造者，每次复制一份再修改，这接近下一篇要讲的**原型模式**。

## 6. 小结

- 参数很多的对象，用多参数构造函数容易传错。实测：宽和高写反，ffmpeg 照样执行，输出了 360×640 的竖屏视频；宽度 641，libx264 要到转码时才报错
- **链式建造者**：每个参数带名字，默认值集中，`build()` 统一检查必填项和参数之间的约束，得到完整、不可变的产品；错误在执行之前就被发现
- **GoF 原版**：指挥者定义步骤，建造者实现步骤，换一个建造者得到另一种表示（命令行 / JSON）
- **C++20 指定初始化**：结构体加上带名字的初始化，适合没有约束的配置；必须按声明顺序写（GCC 报错，Clang 只警告）
- **强类型参数**让相邻的同类型参数不可能写反；**类型状态建造者**在编译期保证必填项
- 参数少就用构造函数，没有约束用指定初始化，有约束用链式建造者

## 7. 练习

1. 下面的调用分别表示什么？你能不看构造函数的声明说出来吗？改成链式建造者会是什么样子？

   <!-- run: off -->
   ```cpp
   EncodeJob a("in.mp4", "out.mp4", "libx264", 23, "medium", 1280, 720, false);
   EncodeJob b("in.mp4", "out.mp4", "libx264", 18, "slow", 0, 0, true);
   ```

2. 给 2.2 节的建造者加上音频码率：`audioBitrate(int kbps)` 设置 AAC 的码率，`noAudio()` 去掉音频。如果两个都调用了，`build()` 应该报错。写出修改后的代码。

3. 用第 3 节的 `Director` 和 `JsonBuilder` 生成「存档版」的 JSON。输出是什么？如果要支持「输出成 GIF 动图」（GIF 需要设置帧率和调色板，没有 CRF 和音频），现在的建造者接口还够用吗？

4. 4.3 节的类型状态建造者里，设置函数和 `build()` 都加了 `&&`。如果去掉它们，下面的代码能编译吗？会有什么问题？

   <!-- run: off -->
   ```cpp
   JobBuilder<false, false> b;
   auto b2 = b.input("a.mp4");
   auto b3 = b2.output("a_out.mp4");
   auto job1 = b3.build();
   auto job2 = b3.build();          // 再 build 一次
   ```

<details>
<summary>点击查看答案</summary>

1. 对照构造函数的声明才知道：`a` 是 CRF 23、medium 预设、缩放到 1280×720、**不要音频**；`b` 是 CRF 18、slow 预设、**不缩放**（0 表示保持原尺寸）、保留音频。`0, 0` 和 `false` 的含义完全藏在参数位置里。改成建造者：

   <!-- run: off -->
   ```cpp
   auto a = EncodeJobBuilder().input("in.mp4").output("out.mp4").crf(23).size(1280, 720).noAudio().build();
   auto b = EncodeJobBuilder().input("in.mp4").output("out.mp4").crf(18).preset("slow").build();
   ```

   `codec` 和 `preset` 的默认值不用再写，「不缩放」就是不调用 `size()`，比 `0, 0` 清楚得多。

2. 在草稿里加一个可选的音频码率，在 `build()` 里加一条检查：

   <!-- run: off -->
   ```cpp
   struct EncodeJob {
       // ……
       bool audio = true;
       std::optional<int> audioKbps;
   };

   EncodeJobBuilder& audioBitrate(int kbps) { job_.audioKbps = kbps; return *this; }
   EncodeJobBuilder& noAudio() { job_.audio = false; return *this; }

   BuildResult build() const {
       // ……原有的检查
       if (!job_.audio && job_.audioKbps) return {std::nullopt, "已经去掉音频，就不能再设置音频码率"};
       return {job_, ""};
   }

   // commandLine() 里：
   cmd += audio ? " -c:a aac" + (audioKbps ? " -b:a " + std::to_string(*audioKbps) + "k" : std::string()) : " -an";
   ```

   这正是建造者的价值：两个设置函数各自都没错，冲突只有把所有参数放在一起看才能发现，而 `build()` 正是那个「放在一起看」的地方。

3. 输出：

   ```text
   {"input": "bbb-10s.mp4", "codec": "libx264", "crf": 18, "preset": "slow", "audio_kbps": 192, "output": "archive.mp4"}
   ```

   存档版不缩放，所以没有 `size`。GIF 不够用：它需要「帧率」「调色板」这样的新步骤，而 `video()` 强制要求 CRF 和预设，`audio()` 对 GIF 也没有意义。要么给接口加上新的步骤（所有已有的建造者都要实现，又是上一篇说的「加新产品」的代价），要么承认 GIF 是另一种产品，给它单独设计一套步骤。指挥者和建造者接口适合「步骤稳定、表示多样」的情况。

4. 去掉 `&&` 以后能编译，但有两个问题：
   - `input()` 和 `output()` 里用 `std::move(job_)` 把草稿移走了，调用之后原来的建造者 `b`、`b2` 里只剩下「被移走后」的字符串（标准只保证它处于有效但未指定的状态，实际上通常是空的），再用它们就会得到错误的结果，而编译器不会提醒你；
   - `build()` 同样移走了草稿，`job2` 拿到的很可能是一个输入和输出都为空的任务。
   加上 `&&` 以后，这些函数只能在右值上调用，上面这几行全都编译不过（`b.input(...)` 里的 `b` 是左值），迫使使用者写成 `JobBuilder<false, false>().input(...).output(...).build()` 这样一路点下去的形式，建造者用完即弃，不会被误用。

</details>
