---
title: 模板方法模式
date: 2026-11-11 23:00:00
updated: 2026-11-11 23:00:00
categories:
  - 设计模式
  - 行为型模式
tags:
  - 设计模式
  - 模板方法
  - NVI
  - 钩子
  - CRTP
  - FFmpeg
order: 4
description: 视频编辑器要把素材导出成 MP4、GIF、WAV，三个导出函数都是「打开输入 → 解码 → 处理 → 编码 → 写文件 → 清理」，复制粘贴写成三份：58 行里有 12 行三份一字不差，加一个「进度回调和取消」要改 3 个函数、10 处。改成模板方法（NVI：公有的非虚 run() 写流程，受保护的虚函数写步骤）后，同样的需求只改基类，子类一行不动。再实测三个坑：「重写时必须调基类」的写法漏调一次，每个任务漏 2 个文件描述符，批量导出到第 127 个就失败，NVI 写法跑 1000 个都没事；构造和析构函数里调虚函数调到的是基类版本，间接调到纯虚函数时程序直接终止，Clang 和 GCC 的表现还不一样；钩子按采样调用时，虚函数比按缓冲区调用慢 5～13 倍。最后用真正的 FFmpeg 实现三个导出任务，并比较 CRTP、std::function 写法，以及模板方法和策略、工厂方法、建造者的区别。
top_group_list: true
disableNunjucks: true
viz: [dp-template-method]
---

> **本文目标**：理解模板方法模式——基类固定算法骨架，将可变步骤延迟到子类；会用 NVI 惯用法（公有非虚接口 + 受保护虚函数）消除复制粘贴流程；分清基本操作与钩子；理解好莱坞原则与两大经典坑（call super 反模式、构造/析构调虚函数）；掌握现代 C++ 的 CRTP 与 `std::function` 写法及钩子粒度开销；分清模板方法与策略、工厂方法、建造者。
>
> **前置知识**：《组合优于继承与 C++ 的四种多态》《策略模式》《简单工厂与工厂方法》。
>
> **编译环境**：`g++ / clang++ -std=c++17`，完整 FFmpeg 工程与压测代码见 `projects/dp-lab/21-template-method/`。

## 1. 问题：三个导出任务，一套流程抄了三遍

### 1.1 复制粘贴版

视频编辑器支持把素材导出成 MP4（H.264）、GIF（表情包）、WAV（纯音频），三个功能的流程完全相同：打开输入 → 选流开解码器 → 开编码器和输出文件 → 循环读包解码 → 处理帧 → 编码写文件 → 冲刷剩余帧并关闭文件。

最省事的做法是写完 `exportMp4`，复制粘贴两份改成 `exportGif` 和 `exportWav`。下面用极简模拟媒体库（240 视频包 + 470 音频包，视频解码有 2 帧延迟）演示这种结构：

```cpp
// 《模板方法模式》：问题版——三个导出函数流程相同，整段复制粘贴后各改几行
#include <cstdio>
#include <string>

enum class Media { Video, Audio };
struct Frame { Media type; int index, w, h; };
int g_open = 0;

struct Demuxer {                                 // 模拟 240 视频包 + 470 音频包交错存放
    int v = 0, a = 0;
    bool open(const std::string& p) { ++g_open; return !p.empty(); }
    bool read(Media& t) {
        if (v == 240 && a == 470) return false;
        t = (a == 470 || (v < 240 && v * 470 <= a * 240)) ? (++v, Media::Video) : (++a, Media::Audio);
        return true;
    }
    void close() { --g_open; }
};

struct Decoder {                                 // 视频解码器有 2 帧延迟，结束前要冲刷
    Media type; int pending = 0, next = 0;
    explicit Decoder(Media t) : type(t) {}
    bool take(int keep, Frame& f) {
        if (pending <= keep) return false;
        --pending; f = {type, next++, 1280, 720}; return true;
    }
    bool decode(Frame& f) { ++pending; return take(type == Media::Video ? 2 : 0, f); }
    bool flush(Frame& f) { return take(0, f); }
};

int encodeBytes(const std::string& codec, const Frame& f) {
    if (codec == "gif") return f.w * f.h / 3;
    if (codec == "pcm_s16le") return 1024 * 2 * 2;
    return f.w * f.h / 40;                       // h264
}

void exportMp4(const std::string& in, const std::string& out) {
    Demuxer dm; if (!dm.open(in)) return;
    ++g_open; Decoder dec(Media::Video); Media t; Frame f; int n = 0; long bytes = 0;
    while (dm.read(t)) if (t == Media::Video && dec.decode(f)) { bytes += encodeBytes("h264", f); ++n; }
    while (dec.flush(f)) { bytes += encodeBytes("h264", f); ++n; }
    --g_open; dm.close();
    std::printf("%-10s %4d 帧 %9ld 字节\n", out.c_str(), n, bytes);
}

void exportGif(const std::string& in, const std::string& out) {
    Demuxer dm; if (!dm.open(in)) return;
    ++g_open; Decoder dec(Media::Video); Media t; Frame f; int n = 0; long bytes = 0;
    while (dm.read(t)) {
        if (t != Media::Video || !dec.decode(f) || f.index % 2 != 0) continue;
        f.w = 480; f.h = 270; bytes += encodeBytes("gif", f); ++n;
    }
    while (dec.flush(f)) {                       // 隔帧丢、缩小在冲刷循环里又抄了一遍
        if (f.index % 2 != 0) continue;
        f.w = 480; f.h = 270; bytes += encodeBytes("gif", f); ++n;
    }
    --g_open; dm.close();
    std::printf("%-10s %4d 帧 %9ld 字节\n", out.c_str(), n, bytes);
}

void exportWav(const std::string& in, const std::string& out) {
    Demuxer dm; if (!dm.open(in)) return;
    ++g_open; Decoder dec(Media::Audio); Media t; Frame f; int n = 0; long bytes = 0;
    while (dm.read(t)) if (t == Media::Audio && dec.decode(f)) { bytes += encodeBytes("pcm_s16le", f); ++n; }
    while (dec.flush(f)) { bytes += encodeBytes("pcm_s16le", f); ++n; }
    --g_open; dm.close();
    std::printf("%-10s %4d 帧 %9ld 字节\n", out.c_str(), n, bytes);
}

int main() {
    exportMp4("bbb-10s.mp4", "out.mp4");
    exportGif("bbb-10s.mp4", "out.gif");
    exportWav("bbb-10s.mp4", "out.wav");
    std::printf("打开着的文件：%d\n", g_open);
}
```

```text
out.mp4     240 帧   5529600 字节
out.gif     120 帧   5184000 字节
out.wav     470 帧   1925120 字节
打开着的文件：0
```

三个函数并排看，7 步里只有 3 步不同，其余 14 格都是同一段流程抄了三遍：

<figure class="diagram">
<svg viewBox="0 0 710 266" role="img" aria-label="三个导出任务的 7 个步骤：选流、编码器两步三个任务各不相同，处理这一帧只有 GIF 不同，其余都相同"><g style="font-family:var(--mono)"><text x="255.0" y="20.0" text-anchor="middle" style="fill:var(--fg);font-size:12px;font-weight:700">导出 MP4</text><text x="435.0" y="20.0" text-anchor="middle" style="fill:var(--fg);font-size:12px;font-weight:700">导出 GIF</text><text x="615.0" y="20.0" text-anchor="middle" style="fill:var(--fg);font-size:12px;font-weight:700">导出 WAV</text><text x="0.0" y="52.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">1 打开输入</text><rect x="170" y="34" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="255.0" y="50.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><rect x="350" y="34" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="435.0" y="50.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><rect x="530" y="34" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="615.0" y="50.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><text x="0.0" y="82.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">2 选流、开解码器</text><rect x="170" y="64" width="170" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="255.0" y="80.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">视频流</text><rect x="350" y="64" width="170" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="435.0" y="80.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">视频流</text><rect x="530" y="64" width="170" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="615.0" y="80.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">音频流</text><text x="0.0" y="112.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">3 开编码器、输出文件</text><rect x="170" y="94" width="170" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="255.0" y="110.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">h264</text><rect x="350" y="94" width="170" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="435.0" y="110.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">gif</text><rect x="530" y="94" width="170" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="615.0" y="110.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">pcm_s16le</text><text x="0.0" y="142.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">4 读包、解码</text><rect x="170" y="124" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="255.0" y="140.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><rect x="350" y="124" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="435.0" y="140.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><rect x="530" y="124" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="615.0" y="140.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><text x="0.0" y="172.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">5 处理这一帧</text><rect x="170" y="154" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="255.0" y="170.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><rect x="350" y="154" width="170" height="24" rx="3" style="fill:var(--warn);fill-opacity:.16;stroke:var(--warn);stroke-width:1.3"/><text x="435.0" y="170.0" text-anchor="middle" style="fill:var(--warn);font-size:11.5px;font-weight:700">隔帧丢、缩小</text><rect x="530" y="154" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="615.0" y="170.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><text x="0.0" y="202.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">6 编码、写文件</text><rect x="170" y="184" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="255.0" y="200.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><rect x="350" y="184" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="435.0" y="200.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><rect x="530" y="184" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="615.0" y="200.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><text x="0.0" y="232.0" text-anchor="start" style="fill:var(--muted);font-size:11.5px">7 冲刷、关闭</text><rect x="170" y="214" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="255.0" y="230.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><rect x="350" y="214" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="435.0" y="230.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><rect x="530" y="214" width="170" height="24" rx="3" style="fill:none;stroke:var(--border);stroke-width:1"/><text x="615.0" y="230.0" text-anchor="middle" style="fill:var(--muted);font-size:11.5px">相同</text><text x="0.0" y="260.0" text-anchor="start" style="fill:var(--muted);font-size:11px">7 步里只有 3 步不同，不同的部分加起来 7 格；其余 14 格是同一段代码抄了三遍</text></g></svg>
</figure>

### 1.2 重复的代价：散弹式修改

在完整版 `projects/dp-lab/21-template-method/copy_paste.cpp` 里，三个导出函数共 58 行，其中 **12 行一字不差**（36 行是纯拷贝）。更糟的是加公共需求：比如增加「进度回调和中途取消（取消时关文件并删掉半成品）」，`copy_paste_progress.cpp` 统计结果显示需要**改动 3 个函数、10 处、新增 20 行、改写旧代码 3 行**。只要有一处漏改清理逻辑，三种格式的行为就会悄悄分裂。

## 2. 模板方法模式

### 2.1 意图与角色

> **GoF**：定义一个操作中算法的骨架，而将一些步骤延迟到子类中。模板方法使得子类可以不改变一个算法的结构即可重定义该算法的某些特定步骤。

大白话：**流程写在基类里只写一遍，会变的步骤做成虚函数留给子类填。** 三个核心概念：

- **模板方法（Template Method）**：写死流程的公有**非虚**函数（`run()`），子类不能改乱顺序；
- **基本操作（Primitive Operation）**：每个子类必须回答的**纯虚**函数（`media()`、`codec()`），漏写直接编译失败；
- **钩子（Hook）**：有默认实现的虚函数（`process()`、`onFinished()`），子类按需重写，不关心就沿用默认行为。

### 2.2 用模板方法重构

把公共流程收拢到基类 `ExportJob::run()`，采用 **NVI（Non-Virtual Interface，非虚接口）** 写法——公有非虚函数调受保护的虚函数：

```cpp
// 《模板方法模式》：NVI 重构——基类公有非虚 run() 固定流程，受保护虚函数留给子类填空
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

enum class Media { Video, Audio };
struct Frame { Media type; int index, w, h; };
int g_open = 0;

struct Demuxer {
    int v = 0, a = 0;
    bool open(const std::string& p) { ++g_open; return !p.empty(); }
    bool read(Media& t) {
        if (v == 240 && a == 470) return false;
        t = (a == 470 || (v < 240 && v * 470 <= a * 240)) ? (++v, Media::Video) : (++a, Media::Audio);
        return true;
    }
    void close() { --g_open; }
};

struct Decoder {
    Media type; int pending = 0, next = 0;
    explicit Decoder(Media t) : type(t) {}
    bool take(int keep, Frame& f) {
        if (pending <= keep) return false;
        --pending; f = {type, next++, 1280, 720}; return true;
    }
    bool decode(Frame& f) { ++pending; return take(type == Media::Video ? 2 : 0, f); }
    bool flush(Frame& f) { return take(0, f); }
};

class ExportJob {
public:
    virtual ~ExportJob() = default;
    bool run(const std::string& in, const std::string& out) {   // 模板方法：公有、非虚
        Demuxer dm; if (!dm.open(in)) return false;
        ++g_open; Decoder dec(media()); Media t; Frame f;
        int frames = 0; long bytes = 0;
        auto emit = [&](Frame& fr) {
            if (!process(fr)) return;
            std::string c = codec();
            bytes += (c == "gif") ? fr.w * fr.h / 3 : (c == "pcm_s16le") ? 4096 : fr.w * fr.h / 40;
            ++frames;
        };
        while (dm.read(t)) if (t == media() && dec.decode(f)) emit(f);
        while (dec.flush(f)) emit(f);
        --g_open; dm.close();
        onFinished(out, frames, bytes);
        return true;
    }

protected:
    virtual Media media() const = 0;                     // 基本操作：必须实现
    virtual const char* codec() const = 0;
    virtual bool process(Frame&) { return true; }        // 钩子：默认原样保留
    virtual void onFinished(const std::string& out, int frames, long bytes) {
        std::printf("%-10s %4d 帧 %9ld 字节\n", out.c_str(), frames, bytes);
    }
};

class Mp4Export : public ExportJob {
protected:
    Media media() const override { return Media::Video; }
    const char* codec() const override { return "h264"; }
};
class GifExport : public ExportJob {
protected:
    Media media() const override { return Media::Video; }
    const char* codec() const override { return "gif"; }
    bool process(Frame& f) override {
        if (f.index % 2 != 0) return false;              // 24 fps 降到 12 fps，并缩小
        f.w = 480; f.h = 270; return true;
    }
};
class WavExport : public ExportJob {
protected:
    Media media() const override { return Media::Audio; }
    const char* codec() const override { return "pcm_s16le"; }
};

int main() {
    std::vector<std::pair<std::unique_ptr<ExportJob>, std::string>> jobs;
    jobs.emplace_back(std::make_unique<Mp4Export>(), "out.mp4");
    jobs.emplace_back(std::make_unique<GifExport>(), "out.gif");
    jobs.emplace_back(std::make_unique<WavExport>(), "out.wav");
    for (auto& [job, out] : jobs) job->run("bbb-10s.mp4", out);
    std::printf("打开着的文件：%d\n", g_open);
}
```

```text
out.mp4     240 帧   5529600 字节
out.gif     120 帧   5184000 字节
out.wav     470 帧   1925120 字节
打开着的文件：0
```

现在 `GifExport` 的隔帧丢与缩小只写一遍，读包和冲刷循环共用 `emit`；`Mp4Export`、`WavExport` 不需要处理帧就不必重写 `process()`。

### 2.3 类图和时序图

```uml
label: 模板方法：抽象类 ExportJob 的 run 是公有的非虚函数，调用受保护的虚函数 media、codec、process、onFinished；Mp4Export、GifExport、WavExport 继承它，只重写自己不同的步骤
classes:
  - { id: Job, name: ExportJob, stereo: abstract, x: 250, y: 20, w: 280, ops: ['+ run(in, out) : bool', '*# media() : Media', '*# codec() : const char*', '# process(f) : bool', '# onFinished(out, n, bytes)', '- encodeOne(f, enc, w)'] }
  - { id: Mp4, name: Mp4Export, x: 20, y: 260, w: 210, ops: ['# media()', '# codec()'] }
  - { id: Gif, name: GifExport, x: 285, y: 260, w: 210, ops: ['# media()', '# codec()', '# process(f)'] }
  - { id: Wav, name: WavExport, x: 550, y: 260, w: 210, ops: ['# media()', '# codec()'] }
rels:
  - { from: Mp4, to: Job, type: inherit, mid: 220, tdx: -80 }
  - { from: Gif, to: Job, type: inherit, mid: 220 }
  - { from: Wav, to: Job, type: inherit, mid: 220, tdx: 80 }
notes:
  - { x: 560, y: 20, w: 200, text: ['run() {', '  打开输入（media()）', '  打开编码器（codec()）', '  循环：解码 → process(f)', '        → 编码、写文件', '  冲刷、关闭', '  onFinished(...)', '}'], to: Job }
```

```uml
label: 时序图：main 调用 GifExport 对象的 run；run 是基类的代码，它先后调用 media、codec，在循环里对每一帧调用 process，最后调用 onFinished；这些调用都是基类调子类
participants:
  - { id: m, name: 'main', x: 20, w: 80 }
  - { id: b, name: 'ExportJob 部分', x: 200, w: 150 }
  - { id: g, name: 'GifExport 部分', x: 470, w: 150 }
messages:
  - { from: m, to: b, text: 'run(in, out)', n: 1 }
  - { from: b, to: g, text: 'media()', n: 2 }
  - { from: g, to: b, text: 'Video', type: reply }
  - { from: b, to: g, text: 'codec()', n: 3 }
  - { from: g, to: b, text: '"gif"', type: reply }
  - { from: b, to: g, text: 'process(f)', n: 4 }
  - { from: g, to: b, text: 'true / false', type: reply }
  - { from: b, to: b, text: 'encode、write', n: 5 }
  - { from: b, to: b, text: 'onFinished(...)', n: 6, note: '没重写，执行基类的默认版本' }
  - { from: b, to: m, text: 'true', type: reply }
frags:
  - { from: 5, to: 7, label: 'loop [每一帧]' }
```

### 2.4 加进度回调、好莱坞原则与钩子设计

加上「进度回调与取消」后，模板方法版只改 `ExportJob` 一个类，三个子类一行不动（`template_method_progress.cpp`）：

<figure class="diagram">
<svg viewBox="0 0 710 126" role="img" aria-label="加进度回调和取消要改的代码（不算 main）：复制粘贴版 10 处改动、新增 20 行、改了 3 个函数；模板方法版 6 处、新增 9 行、只改了基类 1 个类"><g style="font-family:var(--mono)"><text x="108.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">改动处数</text><rect x="108.0" y="30.0" width="100.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="214.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">10 处</text><rect x="108.0" y="60.0" width="60.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="174.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">6 处</text><text x="308.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">新增行数</text><rect x="308.0" y="30.0" width="100.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="414.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">20 行</text><rect x="308.0" y="60.0" width="45.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="359.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">9 行</text><text x="508.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">要改的函数/类</text><rect x="508.0" y="30.0" width="90.0" height="20.0" rx="2" style="fill:var(--warn)"/><text x="604.0" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">3 个</text><rect x="508.0" y="60.0" width="30.0" height="20.0" rx="2" style="fill:var(--accent)"/><text x="544.0" y="75.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">1 个</text><text x="100.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">复制粘贴版</text><text x="100.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">模板方法版</text><text x="10.0" y="118.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">measure.sh：diff -U0 比较加需求前后的文件，去掉第一行注释和 main；相邻的 hunk 算一处。Clang 和 GCC 上结果相同</text></g></svg>
</figure>

| 加进度回调和取消（不算 `main`） | 改动几处 | 改写旧代码 | 新增 | 改到的函数 / 类 |
| --- | --- | --- | --- | --- |
| 复制粘贴版 | 10 处 | 3 行 | 20 行 | 3 个函数 |
| 模板方法版 | 6 处 | 0 行 | 9 行 | 只有 `ExportJob` |

关键在于改动量**不随导出格式的数量增长**。同时，时序图展示了模板方法背后的**好莱坞原则**（Don't call us, we'll call you）：不是子类调基类驱动流程，而是**基类在合适的时机反向调用子类**。这也是框架（GoogleTest 调 `SetUp/TestBody/TearDown`、Qt 调 `paintEvent`）与普通库的分水岭。

设计扩展点时的取舍：每个子类**必须**回答的问题宁可做成**纯虚基本操作**（漏写或写错签名立即编译报错）；大多数子类不关心的可选步骤做成**钩子**，且子类重写一律加 `override`。

下面的交互演示把基类流程与子类步骤左右对照，点击按钮观察「导出 MP4 / WAV」与「漏调基类」的执行路径：

<div class="dsviz" data-viz="dp-template-method"></div>

### 2.5 真实 FFmpeg 工程实测

在 `projects/dp-lab/21-template-method/ffmpeg/export.cpp` 中，我们用真正的 FFmpeg API 实现了完全相同的结构：

- 基类 `ExportJob::run()`（约 109 行）固定整套 `avformat` / `avcodec` 打开、解码、冲刷、写尾、清理流程；
- 中间层 `VideoExport`（26 行）抽离两个视频任务共用的 `sws_scale` 缩放与像素格式转换；
- `Mp4Export`（12 行）、`GifExport`（11 行）、`WavExport`（36 行）只需配置编码器、重写 `keep()` 或 `swr_convert`，三个子类加起来不到 60 行。

对 `bbb-10s.mp4` 实测结果（7 次中位数）：

| 任务 | 解码 / 编码 | 输出 | Clang / macOS（FFmpeg 9.0） | GCC / Ubuntu（FFmpeg 6.1） |
| --- | --- | --- | --- | --- |
| `out.mp4` | 240 / 240 帧 | H.264 640×360 | 368 ms，643 823 字节 | 477 ms，641 202 字节 |
| `out.gif` | 240 / 120 帧 | GIF 320×180 | 365 ms，1 504 852 字节 | 379 ms，1 504 852 字节 |
| `out.wav` | 469 / 469 帧 | PCM 48 kHz 立体声 | 8.8 ms，1 920 078 字节 | 10.2 ms，1 921 102 字节 |

再新增「导出 WebM」只需写一个十几行的 `WebmExport` 子类，整套 FFmpeg 管线一行不碰。

## 3. 深入模板方法：NVI 与两大坑

### 3.1 NVI 惯用法与标准库实践

Herb Sutter 在《Virtuality》中给出三条 C++ 虚函数准则：
1. **公有接口是非虚的**（模板方法）：作为基类守住的一道门，参数检查、加锁、计时、进度回调都可以统一收口；
2. **虚函数设为私有或受保护**：C++ 允许子类重写基类的 `private` 虚函数，只有需要复用基类默认实现时才放 `protected`；
3. **析构函数**：需要通过基类指针删除时设为 `public virtual`，否则设为 `protected` 非虚。

标准库 `std::streambuf` 是最经典的 NVI：公有非虚内联函数 `sputc()` 先往缓冲区写，满了才调受保护虚函数 `overflow()`（实测写 100 字节字幕，缓冲区 0 / 16 / 64 字节时 `overflow()` 分别被调用 100 / 5 / 1 次，完整示例见 `projects/dp-lab/21-template-method/streambuf_nvi.cpp`）。FFmpeg 的 `avcodec_send_packet / receive_frame` 也用 C 回调指针表实现了同等结构。

### 3.2 坑一：call super 反模式（漏调基类）

有些框架把步骤做成虚函数，并在文档里约定「重写时**必须**调基类版本」（如 Android 的 `super.onCreate()`）。这种 **call super 反模式** 编译器管不了：一旦子类忘了调，公共清理就会被跳过。下面对比 `call_super` 与 NVI 批量导出 1000 个 GIF（模拟 macOS 默认 256 个文件描述符上限，stdin/stdout/stderr 占 3 个）：

```cpp
// 《模板方法模式》：call super 漏调基类 cleanup() 导致文件描述符泄漏，NVI 写法结构上漏不了
#include <cstdio>
#include <string>

int g_open = 3, g_max_open = 256;                // 模拟 macOS 默认 256 上限（stdin/out/err 占 3 个）
struct File {
    bool open() { if (g_open >= g_max_open) return false; ++g_open; return true; }
    void close() { --g_open; }
};

namespace call_super {                           // 反模式：要求子类重写时记得调 ExportJob::cleanup()
class ExportJob {
public:
    virtual ~ExportJob() = default;
    bool run() { if (!prepare()) return false; cleanup(); return true; }
protected:
    virtual bool prepare() { return in_.open() && (out_.open() || (in_.close(), false)); }
    virtual void cleanup() { out_.close(); in_.close(); }
    File in_, out_;
};
class GifExport : public ExportJob {
protected:
    bool prepare() override { return ExportJob::prepare() && palette_.open(); }
    void cleanup() override { palette_.close(); } // 忘了调 ExportJob::cleanup()：每任务漏 2 个 fd！
    File palette_;
};
}  // namespace call_super

namespace nvi {                                  // NVI：公共清理由 run() 亲自执行，子类只写钩子
class ExportJob {
public:
    virtual ~ExportJob() = default;
    bool run() {
        if (!in_.open()) return false;
        if (!out_.open()) { in_.close(); return false; }
        bool ok = onPrepare();
        onCleanup();                             // 子类清理自己的资源
        out_.close(); in_.close();               // 公共清理永远不会被跳过
        return ok;
    }
protected:
    virtual bool onPrepare() { return true; }
    virtual void onCleanup() {}
    File in_, out_;
};
class GifExport : public ExportJob {
protected:
    bool onPrepare() override { return palette_.open(); }
    void onCleanup() override { palette_.close(); }
    File palette_;
};
}  // namespace nvi

template <class Job>
void batch(const char* name, int n) {
    g_open = 3; int ok = 0;
    for (int i = 1; i <= n; ++i) {
        Job job;
        if (!job.run()) { std::printf("%s：第 %d 个任务失败（Too many open files）\n", name, i); break; }
        ++ok;
    }
    std::printf("%s：成功 %d 个，打开 fd %d 个\n", name, ok, g_open);
}

int main() {
    batch<call_super::GifExport>("调基类的写法", 1000);
    batch<nvi::GifExport>("NVI 写法    ", 1000);
}
```

```text
调基类的写法：第 127 个任务失败（Too many open files）
调基类的写法：成功 126 个，打开 fd 255 个
NVI 写法    ：成功 1000 个，打开 fd 3 个
```

<figure class="diagram">
<svg viewBox="0 0 640 230" role="img" aria-label="批量导出 GIF 时打开着的文件描述符：调基类的写法每个任务漏 2 个，从 3 涨到 255，第 127 个任务打不开文件；NVI 写法一直是 3 个"><g style="font-family:var(--mono)"><line x1="56" y1="180" x2="596" y2="180" style="stroke:var(--border-2)"/><line x1="56" y1="20" x2="56" y2="180" style="stroke:var(--border-2)"/><text x="50.0" y="184.0" text-anchor="end" style="fill:var(--muted);font-size:10.5px">0</text><text x="50.0" y="124.7" text-anchor="end" style="fill:var(--muted);font-size:10.5px">100</text><text x="50.0" y="65.5" text-anchor="end" style="fill:var(--muted);font-size:10.5px">200</text><text x="50.0" y="32.3" text-anchor="end" style="fill:var(--muted);font-size:10.5px">256</text><text x="56.0" y="196.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">0</text><text x="191.0" y="196.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">50</text><text x="326.0" y="196.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">100</text><text x="398.9" y="196.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">127</text><text x="461.0" y="196.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">150</text><text x="596.0" y="196.0" text-anchor="middle" style="fill:var(--muted);font-size:10.5px">200</text><line x1="56" y1="28.3" x2="596" y2="28.3" style="stroke:var(--danger);stroke-dasharray:4 3"/><text x="596.0" y="23.3" text-anchor="end" style="fill:var(--danger);font-size:10.5px">上限 256</text><polyline points="56.0,178.2 396.2,28.9" style="fill:none;stroke:var(--warn);stroke-width:2"/><circle cx="398.9" cy="28.9" r="4" style="fill:var(--danger)"/><text x="406.9" y="44.9" text-anchor="start" style="fill:var(--danger);font-size:11px">第 127 个任务：Too many open files</text><text x="218.0" y="95.0" text-anchor="end" style="fill:var(--warn);font-size:11px;font-weight:700">调基类的写法（漏调一次）</text><polyline points="56.0,178.2 596.0,178.2" style="fill:none;stroke:var(--accent);stroke-width:2"/><text x="596.0" y="172.2" text-anchor="end" style="fill:var(--accent);font-size:11px;font-weight:700">NVI 写法：一直是 3 个</text><text x="326.0" y="224.0" text-anchor="middle" style="fill:var(--muted);font-size:11px">批量导出的第几个 GIF（纵轴：打开着的文件描述符）</text></g></svg>
</figure>

这种 bug 在 `-Wall -Wextra` 下零警告，导出一两个文件一切正常，批量跑到第 127 个（Linux 默认 1024 上限则在第 511 个）才爆出「打不开文件」。**把必须做的事放进模板方法，只给子类留默认空实现的钩子**，才能从结构上杜绝漏调。

### 3.3 坑二：构造和析构函数里不能指望虚函数

C++ 规定：**构造与析构期间，对象的动态类型就是当前正在构造/析构的那个类**。基类构造时子类还没诞生，基类析构时子类已经销毁，此时调虚函数只会落到基类版本：

```cpp
// 《模板方法模式》：构造与析构期间调虚函数，永远分派到基类版本
#include <cstdio>
#include <string>

class ExportJob {
public:
    ExportJob() { std::printf("创建：%s\n", name()); }
    virtual ~ExportJob() { std::printf("销毁：%s\n", name()); removeTemp(); }
    virtual const char* name() const { return "（未命名）"; }
protected:
    virtual void removeTemp() { std::printf("  ExportJob::removeTemp（无临时文件）\n"); }
};

class GifExport : public ExportJob {
public:
    GifExport() : palette_("palette.tmp") { std::printf("  GifExport 构造完成\n"); }
    ~GifExport() override { std::printf("  GifExport 析构\n"); }
    const char* name() const override { return "导出 GIF"; }
protected:
    void removeTemp() override { std::printf("  删除 %s\n", palette_.c_str()); }
private:
    std::string palette_;
};

int main() {
    GifExport job;
    std::printf("运行中：%s\n", job.name());
}
```

```text
创建：（未命名）
  GifExport 构造完成
运行中：导出 GIF
  GifExport 析构
销毁：（未命名）
  ExportJob::removeTemp（无临时文件）
```

`-Wall -Wextra` 下同样没有警告（仅 `clang++ --analyze` 的 `optin.cplusplus.VirtualCall` 可检出）。如果通过普通成员函数**间接**调到了**纯虚函数**（`projects/dp-lab/21-template-method/pure_call.cpp`），编译无警告，运行时直接 `SIGABRT`（退出码 134）：

| 场景 | Clang 21 / macOS（libc++） | GCC 13 / Ubuntu（libstdc++） |
| --- | --- | --- |
| 构造中**间接**调纯虚 | 运行时 `libc++abi: Pure virtual function called!` | 运行时 `pure virtual method called` 并 `terminate` |
| 构造中**直接**调纯虚 | 默认警告，能链接，运行时终止 | 默认警告，**链接阶段直接报 `undefined reference`** |

正确做法：**流程放到普通成员函数（`run()`）里**；基类构造需要的信息通过构造参数传入；每个类在自己的析构函数里清理自己的资源。

### 3.4 什么时候不用模板方法

- **步骤需要自由组合**：比如「隔帧丢/缩放」×「H.264/GIF/WebM」会产生类爆炸，改用策略或组合；
- **需要运行时切换步骤**：继承在编译期定死，运行中无法把 `GifExport` 变成 `Mp4Export`；
- **流程本身频繁变动或继承层次过深**：会触发脆弱基类问题，一般基类 + 一层子类（最多加一层 `VideoExport`）即可。

## 4. 现代 C++ 写法与性能

### 4.1 CRTP 与 `std::function` 组合

除了经典虚函数，现代 C++ 还有两种常用写法：**CRTP**（编译期静态分派，无虚表指针、可完全内联）和 **`std::function` 组合**（把可变步骤变成可运行期自由拼装的策略）：

```cpp
// 《模板方法模式》：现代 C++ 两种写法——CRTP（编译期零开销）与 std::function 组合（运行期自由拼装）
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

struct Frame { int index, w, h; };

// ① CRTP：编译期模板方法，无虚函数、无虚表指针
template <class Derived>
class CrtpJob {
public:
    void run(int frames) {
        auto& self = static_cast<Derived&>(*this);
        int kept = 0; long bytes = 0;
        for (int i = 0; i < frames; ++i) {
            Frame f{i, 1280, 720};
            if (!self.process(f)) continue;
            bytes += self.encode(f); ++kept;
        }
        std::printf("[CRTP] %-4s %4d 帧 %9ld 字节\n", self.name(), kept, bytes);
    }
protected:
    bool process(Frame&) { return true; }        // 钩子默认实现
};
struct CrtpMp4 : CrtpJob<CrtpMp4> {
    const char* name() const { return "MP4"; }
    int encode(const Frame& f) const { return f.w * f.h / 40; }
};
struct CrtpGif : CrtpJob<CrtpGif> {
    const char* name() const { return "GIF"; }
    bool process(Frame& f) { if (f.index % 2) return false; f.w = 480; f.h = 270; return true; }
    int encode(const Frame& f) const { return f.w * f.h / 3; }
};
struct VirtualJob { virtual ~VirtualJob() = default; virtual int encode(const Frame&) const = 0; };

// ② std::function 组合：固定流程 + 运行期拼装步骤（本质是策略模式）
using Step = std::function<bool(Frame&)>;
using Encode = std::function<int(const Frame&)>;

struct ExportPipeline {
    std::string name; std::vector<Step> steps; Encode encode;
    void run(int frames) const {
        int kept = 0; long bytes = 0; Frame out{};
        for (int i = 0; i < frames; ++i) {
            Frame f{i, 1280, 720}; bool keep = true;
            for (const auto& s : steps) if (!(keep = s(f))) break;
            if (!keep) continue;
            bytes += encode(f); ++kept; out = f;
        }
        std::printf("[组合] %4d 帧 %4d×%-4d %9ld 字节  %s\n", kept, out.w, out.h, bytes, name.c_str());
    }
};

int main() {
    CrtpMp4{}.run(240);
    CrtpGif{}.run(240);
    std::printf("sizeof(CrtpMp4)=%zu, sizeof(VirtualJob)=%zu\n", sizeof(CrtpMp4), sizeof(VirtualJob));

    auto everyNth = [](int n) { return [n](Frame& f) { return f.index % n == 0; }; };
    auto scaleTo  = [](int w, int h) { return [w, h](Frame& f) { f.w = w; f.h = h; return true; }; };
    auto h264 = [](const Frame& f) { return f.w * f.h / 40; };
    auto gif  = [](const Frame& f) { return f.w * f.h / 3; };

    std::vector<ExportPipeline> jobs = {
        {"MP4 小尺寸",    {scaleTo(640, 360)},               h264},
        {"GIF 预览 6fps", {everyNth(4), scaleTo(320, 180)},  gif},
    };
    for (const auto& j : jobs) j.run(240);
}
```

```text
[CRTP] MP4   240 帧   5529600 字节
[CRTP] GIF   120 帧   5184000 字节
sizeof(CrtpMp4)=1, sizeof(VirtualJob)=8
[组合]  240 帧  640×360    1382400 字节  MP4 小尺寸
[组合]   60 帧  320×180    1152000 字节  GIF 预览 6fps
```

CRTP 零虚调用但 `CrtpJob<CrtpMp4>` 与 `CrtpJob<CrtpGif>` 无共同基类，不能放进同一个 `vector`；`std::function` 组合能自由拼出「MP4 小尺寸」等新组合而无需新建子类，但各步骤间不好共享状态（如 `SwsContext`）。

### 4.2 钩子的调用开销：看粒度

对 10 秒立体声音频（96 万个 `float` 采样，470 个音频包）做音量增益，对比「每采样调一次钩子」与「每缓冲区（2048 采样）调一次钩子」（完整压测代码见 `projects/dp-lab/21-template-method/bench_hooks.cpp`）：

<figure class="diagram">
<svg viewBox="0 0 740 186" role="img" aria-label="给 10 秒立体声音频调音量，每遍毫秒数。Clang：虚函数每采样 0.607、std::function 0.542、虚函数每缓冲区 0.046、CRTP 0.046；GCC：0.295、0.520、0.057、0.057"><g style="font-family:var(--mono)"><text x="198.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">Clang 21 / macOS</text><rect x="198.0" y="30.0" width="156.1" height="20.0" rx="2" style="fill:var(--warn)"/><text x="360.1" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">0.607 ms</text><rect x="198.0" y="60.0" width="139.4" height="20.0" rx="2" style="fill:var(--warn)"/><text x="343.4" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">0.542 ms</text><rect x="198.0" y="90.0" width="11.8" height="20.0" rx="2" style="fill:var(--accent)"/><text x="215.8" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.046 ms</text><rect x="198.0" y="120.0" width="11.8" height="20.0" rx="2" style="fill:var(--link)"/><text x="215.8" y="135.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">0.046 ms</text><text x="468.0" y="18.0" text-anchor="start" style="fill:var(--fg);font-size:12px;font-weight:700">GCC 13 / Ubuntu</text><rect x="468.0" y="30.0" width="75.9" height="20.0" rx="2" style="fill:var(--warn)"/><text x="549.9" y="45.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">0.295 ms</text><rect x="468.0" y="60.0" width="133.7" height="20.0" rx="2" style="fill:var(--warn)"/><text x="607.7" y="75.0" text-anchor="start" style="fill:var(--warn);font-size:11.5px;font-weight:700">0.520 ms</text><rect x="468.0" y="90.0" width="14.7" height="20.0" rx="2" style="fill:var(--accent)"/><text x="488.7" y="105.0" text-anchor="start" style="fill:var(--accent);font-size:11.5px;font-weight:700">0.057 ms</text><rect x="468.0" y="120.0" width="14.7" height="20.0" rx="2" style="fill:var(--link)"/><text x="488.7" y="135.0" text-anchor="start" style="fill:var(--link);font-size:11.5px;font-weight:700">0.057 ms</text><text x="190.0" y="45.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">虚函数钩子，每个采样</text><text x="190.0" y="75.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">std::function，每个采样</text><text x="190.0" y="105.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">虚函数钩子，每个缓冲区</text><text x="190.0" y="135.0" text-anchor="end" style="fill:var(--fg);font-size:11.5px">CRTP，每个采样</text><text x="10.0" y="178.0" text-anchor="start" style="fill:var(--muted);font-size:10.5px">bench_hooks：96 万个 float 采样乘一个增益，-O2，每次计时跑 20 遍取平均，7 次取中位数；越短越快</text></g></svg>
</figure>

| 每遍毫秒数（96 万采样，`-O2`） | Clang 21 / macOS | GCC 13 / Ubuntu | 说明 |
| --- | ---: | ---: | --- |
| 虚函数钩子（每采样一次） | 0.607 | 0.295 | 阻断 SIMD 向量化（GCC 靠推测去虚化略快） |
| `std::function`（每采样一次） | 0.542 | 0.520 | 无法推测内联，两边都慢 |
| **虚函数钩子（每缓冲区一次）** | **0.046** | **0.057** | **只调 470 次，内层循环完全向量化，与 CRTP 同速** |
| CRTP（每采样一次） | 0.046 | 0.057 | 编译期完全内联 |

结论与《策略模式》一致：**粒度比写法重要**。热循环里的钩子改成按块处理（`processBlock(float*, n)`），虚函数就能既保留运行时多态，又跑出 CRTP 的满速。

### 4.3 怎么选

| 写法 | 适用场景 | 代价 |
| --- | --- | --- |
| 虚函数 + NVI | 默认选择：子类运行期决定、需要放进同一容器 | 继承耦合、粗粒度调用可忽略开销 |
| CRTP | 编译期定型、步骤在热循环内且不方便按块批处理 | 无共同基类、模板报错复杂 |
| `std::function` 组合 | 步骤需自由拼装、运行期替换 | 步骤间不易共享状态、拿不到受保护成员 |
| 复制粘贴 | 只有两份且未来大概率各走各的路 | 过早抽象比适度重复更糟（通常三份再抽） |

## 5. 模板方法和相似的模式

| 模式 | 机制 | 固定什么 | 可变什么 |
| --- | --- | --- | --- |
| **模板方法** | 继承（基类调子类步骤） | 算法流程与顺序 | 流程中的若干步骤 |
| **策略** | 组合（上下文持有算法对象） | 上下文其余逻辑 | 整个算法（可运行时换） |
| **工厂方法** | 继承（常作为模板方法中的一步） | 使用产品的流程 | 创建哪种具体产品 |
| **建造者** | 组合（指挥者按序调建造者） | 建造步骤顺序 | 每步如何构建、产出什么表示 |

## 6. 小结

- 模板方法把公共流程收口到基类只写一遍，消除复制粘贴带来的散弹式修改（加进度回调只改基类，子类零改动）。
- C++ 用 **NVI** 实现：公有非虚模板方法 + 受保护/私有虚函数；必须回答的步骤用纯虚函数，可选扩展点用钩子，重写一律加 `override`。
- 遵循好莱坞原则，杜绝 **call super 反模式**（漏调清理导致 fd 泄漏到第 127 个任务失败）；**构造/析构期间不要调虚函数**。
- 钩子粒度决定性能：按块调用虚函数与 CRTP 一样快；需要自由组合步骤时改用 `std::function` 策略组合。

## 7. 练习

1. **辨析**：下面四个设计分别是模板方法、策略、工厂方法还是建造者？
   - (a) 编码器基类非虚函数 `encode(frame)` 固定执行「检查尺寸 → 调虚函数 `encodeImpl()` → 统计码率」；
   - (b) 封装器 `writeHeader()` 调虚函数 `makeIndexWriter()`，`Mp4Muxer` 返回 `MoovWriter`，`MkvMuxer` 返回 `CuesWriter`；
   - (c) 播放器持有 `std::function<void(Frame&)> postProcess`，用户在设置里切换「降噪 / 锐化」；
   - (d) `ThumbnailDirector::make(b)` 依次调 `b.setSize()`、`b.addFrames()`、`b.addTimecode()`，产出拼图或网页。
2. **设计**：给第 2.2 节的 `ExportJob` 新增「导出封面」子类 `CoverExport`：只把第一帧编码成 `mjpeg`，且**读到第一帧就立刻停下**，不要把 710 个包全读完。基类需要怎么改？其他三个子类要不要改？
3. **找问题**：同事给 `GifExport::process()` 顺手加了 `const`，结果导出的 GIF 变成了 240 帧原尺寸，没有隔帧也没有缩小。问题出在哪？怎么在编译期杜绝？

```cpp
// 《模板方法模式》练习 3：给钩子误加 const，导致 GIF 的处理逻辑完全没生效
#include <cstdio>

struct Frame { int index, w, h; };

class ExportJob {
public:
    virtual ~ExportJob() = default;
    void run(int n) {
        int kept = 0; long bytes = 0;
        for (int i = 0; i < n; ++i) {
            Frame f{i, 1280, 720};
            if (process(f)) { bytes += f.w * f.h / 3; ++kept; }
        }
        std::printf("导出 %d 帧，%ld 字节\n", kept, bytes);
    }
protected:
    virtual bool process(Frame&) { return true; }
};

class GifExport : public ExportJob {
protected:
    bool process(Frame& f) const {               // 顺手多写了 const，没写 override！
        if (f.index % 2 != 0) return false;
        f.w = 480; f.h = 270; return true;
    }
};

int main() { GifExport{}.run(240); }
```

<details>
<summary>点击查看答案</summary>

1. (a) **模板方法**（NVI 固定检查→编码→统计流程）；(b) **工厂方法**（专门负责创建索引写入器对象）；(c) **策略**（组合 + 运行时切换算法）；(d) **建造者**（指挥者按序驱动外部建造者产出不同表示）。

2. 基类只需新增一个默认返回 `false` 的钩子 `virtual bool done(int framesEncoded) const { return false; }`，在读包和冲刷循环条件里加上 `!done(frames)`；`CoverExport` 重写 `done()` 返回 `framesEncoded >= 1`（实测只需读 7 个包就停下，其他三个子类零改动）。这也说明模板方法的局限：未预留的扩展点必须改基类才能加入。完整可运行代码见 `projects/dp-lab/21-template-method/ex2_cover.cpp`。

3. `bool process(Frame&) const` 与基类 `bool process(Frame&)` 签名不同，没有重写而是**隐藏**了基类钩子，`run()` 调到的仍是基类默认版本（240 帧、73 728 000 字节）。根治方法：**重写一律加 `override`**，编译器会立刻报 `marked 'override', but does not override`；若该步骤每个子类都必须实现，直接做成纯虚函数则会在实例化时报错。完整版见 `projects/dp-lab/21-template-method/ex4_override_ce.cpp`。

</details>
