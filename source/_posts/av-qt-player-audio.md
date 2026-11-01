---
title: Qt 播放器：声音、同步与播放控制
date: 2026-11-04 10:00:00
updated: 2026-11-04 10:00:00
categories:
  - 音视频开发
  - 音视频实战
tags:
  - 音视频
  - Qt
  - QAudioSink
  - 音视频同步
  - C++
order: 2
description: 用 QAudioSink 的拉模式播放声音，以声音为主时钟同步画面。第一版用「写入的时间戳减去缓冲区里的数据」算声音时钟，结果 240 帧丢了 107 帧：拉模式下 bytesFree() 永远是 0，声卡又是 85 毫秒一大块地取数据，算出的时钟一跳一跳的。改用 processedUSecs()（声卡已经播放的时长）加上一张「字节位置→时间戳」的对照表后，一帧不丢，音画偏差中位数 −0.3 毫秒。再让声音等第一帧画面显示出来才开始放，偏差最大值从 64 毫秒降到 5.7 毫秒，跳转后也不再丢帧。最后加上暂停、拖动进度条、音量和打开网络流：RTMP 15 毫秒、RTSP 50 毫秒。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：会用 QAudioSink 的拉模式播放 PCM；理解声音时钟怎样计算，以及为什么「看起来对」的算法会让画面频繁丢帧；实现以声音为主时钟的同步；实现暂停、跳转、音量和网络流播放；学会给 GUI 程序加上自动测试用的命令行参数。
>
> **前置知识**：上一篇；第 4 章《音频播放》《音视频同步》《暂停与跳转》。
>
> **实验环境**：macOS 27.0（Apple M6），Qt 6.12.0，FFmpeg 9.0.2，Apple Clang 21，Release 构建。

## 1. QAudioSink

Qt Multimedia 的 `QAudioSink` 负责把 PCM 送给声卡，有两种用法，和第 4 章 SDL 的两种模式一一对应：

- **推模式**：`QIODevice *dev = sink->start();`，程序主动往 `dev` 里写数据，写多了会被拒绝（只写进去 `bytesFree()` 那么多）。
- **拉模式**：`sink->start(myDevice);`，传进去一个自己实现的 `QIODevice`，声卡需要数据时调用它的 `readData()` 来取。和 SDL 的回调模式一样，数据由声卡的节奏驱动。

`qplayer` 用拉模式：`AudioOutput` 继承 `QIODevice`，解码线程往它的缓冲区里写 PCM（统一转换成 48000 Hz、立体声、16 位），声卡从中取。

## 2. 声音时钟：第一版

第 4 章讲过，同步以声音为主时钟：「此刻正在播放的声音的时间戳」就是播放的进度，视频帧的 PTS 到了这个时间就显示。第 4 章用 SDL 时这样算：

> 正在播放的时间戳 = 最后写入的数据的结束时间戳 − 还没被取走的数据的时长 − 设备缓冲区中还没播放的数据的时长

照搬到 Qt：`AudioOutput` 记下缓冲区末尾的时间戳 `m_ptsEnd`，减去自己缓冲区里的字节数，再减去声卡缓冲区里还没播放的字节数 `bufferSize() - bytesFree()`：

```cpp
double AudioOutput::clock() {
    qint64 inSink = m_sink->bufferSize() - m_sink->bytesFree();
    return m_ptsEnd - (double)(m_buf.size() + inSink) / kBytesPerSec;
}
```

看起来没问题。播放 10 秒的测试视频（程序的 `--quit` 参数在指定时间打印统计后退出，见第 5 节）：

```text
显示 133 帧，丢弃 107 帧，声音欠载 1 次，音画偏差 中位 -17.0 ms / 绝对值最大 213.3 ms
```

240 帧丢了 107 帧，画面一卡一卡的。在 `clock()` 和 `readData()` 中打印日志，问题立刻就清楚了：

```text
READ 48000
READ 16384
DBG 0.2280 0.0853 12          ← 墙上时间 0.228 秒，声音时钟 0.0853 秒，帧队列 12 帧
...
DBG 0.5290 0.4267 12
DBG 0.5335 0.4267 12
DBG 0.5398 0.4267 12
...
DBG 0.5835 0.4267 12
READ 16384
DBG 0.5948 0.5120 12
```

- **`bytesFree()` 永远是 0**。拉模式下，`QAudioSink` 的缓冲区（`bufferSize()` 是 48000 字节，0.25 秒）总是被填满的，`bytesFree()` 没有提供任何「声卡播到哪里了」的信息。
- **声卡成块地取数据**：先取 48000 字节填满缓冲区，之后每次取 16384 字节（85.3 毫秒）。

于是算出的时钟在两次 `readData` 之间纹丝不动，每取一次数据就猛地往前跳 85.3 毫秒（0.4267 → 0.5120）。时钟停着的时候，到时间的帧显示不出来；时钟一跳，好几帧同时「迟到」，只能丢掉。

## 3. 用声卡实际播放的进度

`QAudioSink::processedUSecs()` 返回声卡**已经播放**了多少微秒的音频。在日志里打印它，每 10.67 毫秒（512 个采样）平滑地前进一次，这才是真正的播放进度。

但它是「从开始到现在一共播了多久」，不是时间戳：中间可能有为了欠载而补的静音，跳转以后时间戳也会跳变。解决办法是记一张对照表：每次 `readData` 交出一段真实数据，就记下它在交给声卡的字节流中的位置，以及开头的时间戳。查询时，用 `processedUSecs()` 换算出已经播放到字节流的哪个位置，在表中找到对应的段，算出时间戳：

<figure class="diagram">
<svg viewBox="0 0 680 190" role="img" aria-label="声音时钟的计算：交给声卡的字节流由一段段真实数据和补的静音组成，每段真实数据记下起始位置和起始时间戳；processedUSecs 换算成字节位置，落在某段数据中，时间戳等于这段的起始时间戳加上偏移的时长；落在静音中时，时钟停在上一段的结束时间戳">
  <defs>
    <marker id="ac-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--danger)"/></marker>
  </defs>
  <g style="font-family:var(--mono);font-size:12px">
    <text x="10" y="30" style="fill:var(--muted);font-size:10px">交给声卡的字节流（m_pulled 一直累加）</text>
    <g style="stroke:var(--border-2)">
      <rect x="10" y="40" width="190" height="40" style="fill:var(--accent-soft)"/>
      <rect x="200" y="40" width="70" height="40" style="fill:var(--panel-2)"/>
      <rect x="270" y="40" width="230" height="40" style="fill:var(--accent-soft)"/>
      <rect x="500" y="40" width="170" height="40" style="fill:var(--accent-soft)"/>
    </g>
    <text x="20" y="58" style="fill:var(--fg);font-size:11px">段 1</text><text x="20" y="73" style="fill:var(--muted);font-size:10px">pts=6.912</text>
    <text x="210" y="65" style="fill:var(--muted);font-size:11px">静音</text>
    <text x="280" y="58" style="fill:var(--fg);font-size:11px">段 2</text><text x="280" y="73" style="fill:var(--muted);font-size:10px">pts=7.000（跳转后）</text>
    <text x="510" y="58" style="fill:var(--fg);font-size:11px">段 3</text><text x="510" y="73" style="fill:var(--muted);font-size:10px">pts=7.085</text>
    <line x1="350" y1="95" x2="350" y2="82" style="stroke:var(--danger)" stroke-width="2" marker-end="url(#ac-arrow)"/>
    <text x="250" y="112" style="fill:var(--danger);font-size:11px">processedUSecs × 192000 / 10⁶ = 已播放到这里</text>
    <text x="10" y="145" style="fill:var(--fg);font-size:11px">落在某段中：时钟 = 这段的 pts + (位置 − 这段的起点) / 192000</text>
    <text x="10" y="165" style="fill:var(--fg);font-size:11px">落在静音中：时钟停在上一段的结束时间戳（欠载、跳转时画面也跟着停）</text>
    <text x="10" y="185" style="fill:var(--muted);font-size:10px">192000 = 48000 Hz × 2 声道 × 2 字节，一秒的字节数</text>
  </g>
</svg>
</figure>

```cpp
// audiooutput.h：声音输出。解码线程往里写 PCM，QAudioSink 按自己的节奏来读（拉模式）
#pragma once
#include <QIODevice>
#include <QMutex>
#include <QByteArray>
#include <QVector>

class QAudioSink;

class AudioOutput : public QIODevice {
    Q_OBJECT
public:
    explicit AudioOutput(QObject *parent = nullptr);
    ~AudioOutput() override;

    bool start(bool waitVideo);                          // 48000 Hz、立体声、16 位；waitVideo：先压住，等视频第一帧
    void write(const char *data, int bytes, double ptsEnd);  // ptsEnd：这段数据结束处的时间戳
    void clear(bool waitVideo);                          // 跳转时扔掉缓冲的声音，有视频时先压住不放
    void release();                                      // 视频准备好了，开始放声音
    double clock();                                      // 此刻正在播放的时间戳，没有声音时返回 -1
    double bufferedSeconds();
    void setPaused(bool paused);
    void setVolume(float v);
    int underruns = 0;                                   // 声卡要数据而我们拿不出的次数

protected:
    qint64 readData(char *data, qint64 maxSize) override;
    qint64 writeData(const char *, qint64) override { return -1; }
    qint64 bytesAvailable() const override;
    bool isSequential() const override { return true; }

private:
    static constexpr int kBytesPerSec = 48000 * 2 * 2;
    QAudioSink *m_sink = nullptr;
    mutable QMutex m_mutex;
    QByteArray m_buf;          // 还没交给声卡的 PCM
    double m_ptsEnd = -1;      // m_buf 末尾对应的时间戳
    bool m_starving = false;
    bool m_hold = true;        // 刚打开和跳转后先不放声音，等视频的第一帧显示出来

    // 交给声卡的字节流中，每一段真实数据的位置和起始时间戳（其余部分是补的静音）
    struct Segment { qint64 begin, end; double pts; };
    QVector<Segment> m_segs;
    qint64 m_pulled = 0;       // 一共交给声卡多少字节
    double m_lastPts = -1;     // 已经播完的最后一段的结束时间戳
};
```

```cpp
#include "audiooutput.h"
#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>
#include <cstring>

AudioOutput::AudioOutput(QObject *parent) : QIODevice(parent) {}

AudioOutput::~AudioOutput() {
    if (m_sink) m_sink->stop();
}

bool AudioOutput::start(bool waitVideo) {
    m_hold = waitVideo;
    QAudioFormat fmt;
    fmt.setSampleRate(48000);
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Int16);
    QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (dev.isNull() || !dev.isFormatSupported(fmt)) return false;
    m_sink = new QAudioSink(dev, fmt, this);
    open(QIODevice::ReadOnly);
    m_sink->start(this);          // 拉模式：声卡需要数据时调用 readData
    return true;
}

void AudioOutput::write(const char *data, int bytes, double ptsEnd) {
    QMutexLocker lock(&m_mutex);
    m_buf.append(data, bytes);
    m_ptsEnd = ptsEnd;
}

void AudioOutput::clear(bool waitVideo) {
    QMutexLocker lock(&m_mutex);
    m_buf.clear();
    m_ptsEnd = -1;
    m_segs.clear();            // 声卡里还剩一点旧的声音，播完之前 clock() 返回 -1
    m_lastPts = -1;
    m_hold = waitVideo;
}

void AudioOutput::release() {
    QMutexLocker lock(&m_mutex);
    m_hold = false;
}

double AudioOutput::bufferedSeconds() {
    QMutexLocker lock(&m_mutex);
    return (double)m_buf.size() / kBytesPerSec;
}

qint64 AudioOutput::bytesAvailable() const {
    QMutexLocker lock(&m_mutex);
    return m_buf.size() + QIODevice::bytesAvailable();
}

qint64 AudioOutput::readData(char *data, qint64 maxSize) {
    QMutexLocker lock(&m_mutex);
    qint64 n = m_hold ? 0 : qMin<qint64>(maxSize, m_buf.size());
    if (n > 0) {
        double pts = m_ptsEnd - (double)m_buf.size() / kBytesPerSec;   // 这段数据开头的时间戳
        m_segs.append({m_pulled, m_pulled + n, pts});
        memcpy(data, m_buf.constData(), n);
        m_buf.remove(0, n);
    }
    if (n < maxSize) {
        // 数据不够：剩下的填静音，声卡不停；静音没有时间戳，播到这里时钟就停住
        memset(data + n, 0, maxSize - n);
        if (!m_starving && m_ptsEnd >= 0 && !m_hold) underruns++;
        m_starving = true;
    } else {
        m_starving = false;
    }
    m_pulled += maxSize;
    return maxSize;
}

double AudioOutput::clock() {
    QMutexLocker lock(&m_mutex);
    if (!m_sink) return -1;
    // processedUSecs：声卡已经播放了多少微秒的声音（包括补的静音），换算成字节流中的位置
    qint64 played = m_sink->processedUSecs() * kBytesPerSec / 1000000;
    while (!m_segs.isEmpty() && m_segs.first().end <= played) {     // 已经播完的段，记下结束时间戳后扔掉
        const Segment &s = m_segs.first();
        m_lastPts = s.pts + (double)(s.end - s.begin) / kBytesPerSec;
        m_segs.removeFirst();
    }
    if (!m_segs.isEmpty() && played >= m_segs.first().begin)
        return m_segs.first().pts + (double)(played - m_segs.first().begin) / kBytesPerSec;
    return m_lastPts;          // 正在播静音（或者还没开始）
}

void AudioOutput::setPaused(bool paused) {
    if (!m_sink) return;
    if (paused) m_sink->suspend();
    else m_sink->resume();
}

void AudioOutput::setVolume(float v) {
    if (m_sink) m_sink->setVolume(v);
}
```

改完再测：

```text
显示 240 帧，丢弃 0 帧，声音欠载 1 次，音画偏差 中位 -0.3 ms / 绝对值最大 64.0 ms
```

一帧不丢，偏差的中位数只有 −0.3 毫秒。唯一的一次欠载在文件末尾：声音播完了，没有数据可取（在 9 秒时退出，欠载是 0 次）。

注意 `processedUSecs()` 只是声卡**处理**了多少，从声卡处理到扬声器真正发声还有一段输出延迟（硬件缓冲、蓝牙耳机的编解码等），Qt 没有提供查询它的接口。这个延迟是固定的，会让画面整体比声音早一点，但不会造成丢帧和卡顿。第 4 章用 SDL 的 disk 驱动做过精确的验证，这里就不重复了。

## 4. 让声音等一等画面

偏差的最大值 64 毫秒是哪来的？把每一帧的偏差打印出来，只有开头两帧有问题：第 1 帧 −53 毫秒，第 2 帧 −33 毫秒，之后都在 ±6 毫秒以内。原因是打开文件后，声音一有数据就开始播了，第一帧画面却还要等解码、上传纹理；画面显示出来时，声音已经播了 50 多毫秒。

跳转时更明显：用 `--seek 2:7`（2 秒时跳到 7 秒）测试，跳转后丢了 5 帧。精确跳转要先从 7 秒之前的关键帧开始解码，丢掉 7 秒之前的帧（第 4 章《暂停与跳转》），这要花一点时间；声音那边没有这个负担，马上就从 7 秒开始播了，等画面追上来，前面几帧都已经迟到了。

办法是让声音等画面：打开文件和跳转之后，`AudioOutput` 先把声音压住（`readData` 只给静音），等界面线程显示出第一帧，再调用 `release()` 放开。改完以后：

```text
显示 240 帧，丢弃 0 帧，声音欠载 1 次，音画偏差 中位 -0.3 ms / 绝对值最大 5.7 ms
```

偏差的最大值降到了 5.7 毫秒，跳转后也不再丢帧。不过这里有一个陷阱：只有声音、没有画面的文件（比如 `.m4a`），永远等不到「第一帧画面」，声音就被永远压住了。所以 `start()` 和 `clear()` 都带一个参数，只有存在视频流时才压住。

## 5. 播放控制

`MainWindow` 负责界面和同步：

```cpp
// mainwindow.h：播放器窗口。界面、播放控制，以及按时钟显示视频帧
#pragma once
#include <QMainWindow>
#include <QElapsedTimer>
#include <QTimer>
#include <QVector>
#include "videowidget.h"

class Decoder;
class AudioOutput;
class QPushButton;
class QSlider;
class QLabel;
struct VideoFrame;

struct Options {
    VideoWidget::Mode mode = VideoWidget::GL;
    bool mute = false;
    bool barcode = false;      // 读画面顶部的时间条码，统计延迟（第 5 章）
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const Options &opt, QWidget *parent = nullptr);
    ~MainWindow() override;
    void openUrl(const QString &url);
    void togglePause();
    void seek(double seconds);
    QString stats();

private slots:
    void onOpened(double duration, int width, int height, bool hasAudio);
    void onTick();

private:
    double masterClock();
    void display(const VideoFrame &f);
    void updatePosition(double pts);
    void closeMedia();

    Options m_opt;
    VideoWidget *m_video;
    QPushButton *m_playBtn;
    QSlider *m_slider, *m_volume;
    QLabel *m_timeLabel;
    QTimer m_tick;

    Decoder *m_decoder = nullptr;
    AudioOutput *m_audio = nullptr;
    bool m_hasAudio = false, m_hasVideo = false, m_paused = false, m_dragging = false;
    double m_duration = 0;

    // 没有声音时用系统时钟：clock = 现在 - m_sysBase
    QElapsedTimer m_wall;
    double m_sysBase = 0, m_pausedAt = 0;
    int m_serial = -1;         // 正在显示的序号，变了就重新对齐系统时钟
    double m_behindSince = -1; // 直播：从什么时候开始一直落后

    // 统计
    int m_shown = 0, m_dropped = 0;
    QVector<double> m_avDiff;  // 每一帧显示时，画面比声音超前多少秒
    QVector<int> m_latency;    // 条码延迟（毫秒）
};
```

```cpp
#include "mainwindow.h"
#include "decoder.h"
#include "audiooutput.h"
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <ctime>

static QString fmtTime(double s) {
    int t = (int)s;
    return QString("%1:%2").arg(t / 60, 2, 10, QChar('0')).arg(t % 60, 2, 10, QChar('0'));
}

MainWindow::MainWindow(const Options &opt, QWidget *parent) : QMainWindow(parent), m_opt(opt) {
    m_video = new VideoWidget(opt.mode);
    m_playBtn = new QPushButton("暂停");
    m_slider = new QSlider(Qt::Horizontal);
    m_volume = new QSlider(Qt::Horizontal);
    m_volume->setRange(0, 100);
    m_volume->setValue(opt.mute ? 0 : 80);
    m_volume->setMaximumWidth(90);
    m_timeLabel = new QLabel("00:00 / 00:00");

    auto *bar = new QHBoxLayout;
    bar->addWidget(m_playBtn);
    bar->addWidget(m_slider, 1);
    bar->addWidget(m_timeLabel);
    bar->addWidget(new QLabel("音量"));
    bar->addWidget(m_volume);
    auto *central = new QWidget;
    auto *box = new QVBoxLayout(central);
    box->setContentsMargins(0, 0, 0, 6);
    box->addWidget(m_video, 1);
    box->addLayout(bar);
    setCentralWidget(central);
    resize(960, 600);

    QMenu *file = menuBar()->addMenu("文件");
    file->addAction("打开文件…", this, [this] {
        QString f = QFileDialog::getOpenFileName(this, "打开文件");
        if (!f.isEmpty()) openUrl(f);
    });
    file->addAction("打开网络流…", this, [this] {
        QString u = QInputDialog::getText(this, "打开网络流", "URL（rtmp://、rtsp://、http://…）：");
        if (!u.isEmpty()) openUrl(u);
    });

    connect(m_playBtn, &QPushButton::clicked, this, &MainWindow::togglePause);
    connect(m_slider, &QSlider::sliderPressed, this, [this] { m_dragging = true; });
    connect(m_slider, &QSlider::sliderReleased, this, [this] {
        m_dragging = false;
        seek(m_slider->value() / 10.0);
    });
    connect(m_volume, &QSlider::valueChanged, this, [this](int v) {
        if (m_audio) m_audio->setVolume(v / 100.0f);
    });

    // 每 5 毫秒检查一次：队头的帧到时间了没有
    m_tick.setTimerType(Qt::PreciseTimer);
    connect(&m_tick, &QTimer::timeout, this, &MainWindow::onTick);
    m_wall.start();
}

MainWindow::~MainWindow() { closeMedia(); }

void MainWindow::closeMedia() {
    m_tick.stop();
    delete m_decoder;          // 析构函数会停止并等待解码线程
    m_decoder = nullptr;
    delete m_audio;
    m_audio = nullptr;
}

void MainWindow::openUrl(const QString &url) {
    closeMedia();
    setWindowTitle(url);
    m_audio = new AudioOutput(this);
    m_decoder = new Decoder(url, m_audio, this);
    connect(m_decoder, &Decoder::opened, this, &MainWindow::onOpened);    // 跨线程，自动排队到主线程
    connect(m_decoder, &Decoder::failed, this, [this](const QString &msg) { m_timeLabel->setText(msg); });
    m_serial = -1;
    m_paused = false;
    m_decoder->start();
}

void MainWindow::onOpened(double duration, int width, int, bool hasAudio) {
    m_duration = duration;
    m_hasVideo = width > 0;
    m_hasAudio = hasAudio && m_audio->start(width > 0);   // 有视频时，声音等第一帧显示出来再放
    if (m_hasAudio) m_audio->setVolume(m_volume->value() / 100.0f);
    m_slider->setRange(0, (int)(duration * 10));
    m_slider->setEnabled(duration > 0);       // 直播不能拖
    m_tick.start(5);
}

double MainWindow::masterClock() {
    if (m_hasAudio) return m_audio->clock();  // 以声音为主时钟（第 4 章《音视频同步》）
    double now = m_wall.nsecsElapsed() / 1e9;
    return (m_paused ? m_pausedAt : now) - m_sysBase;
}

void MainWindow::onTick() {
    if (!m_decoder || m_paused) return;
    if (!m_hasVideo) {                                // 只有声音：按声音时钟更新进度
        double c = masterClock();
        if (c >= 0) updatePosition(c);
        return;
    }
    VideoFrame f;
    while (m_decoder->peek(f)) {
        if (f.serial != m_decoder->serial) { m_decoder->pop(); continue; }   // 跳转之前的旧帧
        if (f.serial != m_serial) {
            // 新的开始（刚打开或者刚跳转）：先显示第一帧，系统时钟从这一帧算起
            m_serial = f.serial;
            m_sysBase = m_wall.nsecsElapsed() / 1e9 - f.pts;
            display(f);
            m_decoder->pop();
            if (m_hasAudio) m_audio->release();    // 跳转后压住的声音，现在可以放了
            break;
        }
        // 直播追帧：没有声音的直播流，队列里一直积压着 3 帧以上（持续 1 秒），说明落后了，
        // 丢帧直到只剩 1 帧，时钟对齐到新的队头
        if (m_decoder->live && !m_hasAudio) {
            double now = m_wall.nsecsElapsed() / 1e9;
            if (m_decoder->queued() < 3) m_behindSince = -1;
            else if (m_behindSince < 0) m_behindSince = now;
            else if (now - m_behindSince > 1.0) {
                VideoFrame head;
                while (m_decoder->queued() > 1) { m_decoder->pop(); m_dropped++; }
                if (m_decoder->peek(head)) m_sysBase = now - head.pts;
                m_behindSince = -1;
                continue;
            }
        }
        double clock = masterClock();
        if (clock < 0) break;                         // 声音还没开始
        if (f.pts > clock + 0.005) break;             // 还没到时间
        VideoFrame next;
        if (m_decoder->peekNext(next) && next.serial == f.serial && next.pts <= clock) {
            m_decoder->pop();                         // 下一帧也到时间了：这一帧来不及显示，丢掉
            m_dropped++;
            continue;
        }
        display(f);
        m_decoder->pop();                             // setFrame 已经引用了这一帧，可以从队列里删掉
        break;
    }
}

static uint32_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

// 和第 5 章 latency_probe 相同的条码：40 个 32×32 的方块
static int readCode(const AVFrame *f, uint32_t *ms) {
    if (f->width < 1280 || f->height < 32) return -1;
    uint64_t v = 0;
    for (int b = 0; b < 40; b++) {
        int sum = 0;
        for (int y = 12; y < 20; y++)
            for (int x = 12; x < 20; x++) sum += f->data[0][y * f->linesize[0] + b * 32 + x];
        v = (v << 1) | (sum / 64 > 128);
    }
    uint32_t t = (uint32_t)(v >> 8);
    uint8_t s = (t & 0xff) + ((t >> 8) & 0xff) + ((t >> 16) & 0xff) + (t >> 24);
    if ((uint8_t)~s != (uint8_t)(v & 0xff)) return -1;
    *ms = t;
    return 0;
}

void MainWindow::display(const VideoFrame &f) {
    m_video->setFrame(f.frame);
    m_shown++;
    if (m_hasAudio) {
        double a = m_audio->clock();
        if (a >= 0) m_avDiff.append(f.pts - a);
    }
    uint32_t sent;
    if (m_opt.barcode && readCode(f.frame, &sent) == 0) m_latency.append((int32_t)(nowMs() - sent));
    updatePosition(f.pts);
}

void MainWindow::updatePosition(double pts) {
    if (!m_dragging && m_duration > 0) m_slider->setValue((int)(pts * 10));
    m_timeLabel->setText(m_duration > 0 ? fmtTime(pts) + " / " + fmtTime(m_duration)
                                        : "直播 " + fmtTime(pts));
}

void MainWindow::togglePause() {
    if (!m_decoder) return;
    m_paused = !m_paused;
    double now = m_wall.nsecsElapsed() / 1e9;
    if (m_paused) m_pausedAt = now;
    else m_sysBase += now - m_pausedAt;       // 暂停了多久，系统时钟的起点就往后挪多久
    if (m_hasAudio) m_audio->setPaused(m_paused);
    m_playBtn->setText(m_paused ? "播放" : "暂停");
}

void MainWindow::seek(double seconds) {
    if (m_decoder && m_duration > 0) m_decoder->seek(qBound(0.0, seconds, m_duration));
}

QString MainWindow::stats() {
    auto median = [](QVector<double> v) { std::sort(v.begin(), v.end()); return v.isEmpty() ? 0 : v[v.size() / 2]; };
    QString s = QString("显示 %1 帧，丢弃 %2 帧，重绘 %3 次、每次 %4 ms").arg(m_shown).arg(m_dropped)
                    .arg(m_video->paints).arg(m_video->uploadMs / qMax(1, m_video->paints), 0, 'f', 2);
    if (m_hasAudio) {
        QVector<double> absd;
        for (double d : m_avDiff) absd.append(std::fabs(d));
        double mx = absd.isEmpty() ? 0 : *std::max_element(absd.begin(), absd.end());
        s += QString("，声音欠载 %1 次，音画偏差 中位 %2 ms / 绝对值最大 %3 ms")
                 .arg(m_audio->underruns).arg(median(m_avDiff) * 1000, 0, 'f', 1).arg(mx * 1000, 0, 'f', 1);
    }
    if (!m_latency.isEmpty()) {
        QVector<double> l;
        for (int x : m_latency) l.append(x);
        std::sort(l.begin(), l.end());
        s += QString("，条码延迟 最小 %1 / 中位 %2 / 最大 %3 ms").arg(l.first()).arg(l[l.size() / 2]).arg(l.last());
    }
    return s;
}
```

- **`onTick` 是同步的核心**：每 5 毫秒检查一次队头的帧。序号不对的是跳转之前的旧帧，直接扔掉；序号变了（刚打开、刚跳转）就立即显示第一帧，并放开压住的声音；之后的帧，PTS 到了主时钟才显示；如果下一帧也已经到时间了，说明这一帧来不及显示，丢掉（第 4 章《音视频同步》）。
- **主时钟**：有声音时用声音时钟；没有声音（比如第 5 章推的直播流只有画面）时用系统时钟，`clock = 现在 − m_sysBase`，第一帧显示时把 `m_sysBase` 对齐到这一帧的 PTS。
- **暂停**：声音调用 `QAudioSink::suspend()`，`processedUSecs()` 随之停止，声音时钟自然就停了；系统时钟则在恢复时把起点往后挪暂停的时长。
- **跳转**：拖动进度条松手时调用 `Decoder::seek`，解码线程定位到目标之前的关键帧，清空解码器、帧队列和声音缓冲区，序号加一，然后丢掉目标之前的帧（精确跳转）。
- **音量**：`QAudioSink::setVolume()`，0 到 1。
- **直播追帧**：直播流没有进度条（`m_slider->setEnabled(duration > 0)`），时间显示为「直播 mm:ss」。没有声音的直播流如果持续落后，会丢帧追到最新，下一篇用自己写的直播服务器测试它。

`main.cpp` 处理命令行参数。除了要播放的文件或 URL，还有一组「定时动作」，让播放器能在没有人操作的情况下完成测试：`--quit 11` 在 11 秒时打印统计并退出，`--grab 3:a.png` 在 3 秒时截图，`--seek 2:7` 在 2 秒时跳到 7 秒，`--pause 4:5` 在 4 秒时暂停、5 秒时继续，`--mute` 静音（测试时不发出声音），`--barcode` 读出画面顶部第 5 章的时间条码、统计延迟。本章所有的数据都是这样测出来的：

<details>
<summary>main.cpp</summary>

```cpp
// main.cpp：命令行参数。除了正常打开文件，还支持一些自动测试用的参数：
//   --render image   用 QImage 显示（默认用 OpenGL 着色器）
//   --mute           静音   --barcode  统计条码延迟
//   --quit N         N 秒后打印统计信息并退出
//   --grab N:文件    N 秒时把窗口截图保存下来
//   --seek N:T       N 秒时跳转到 T 秒     --pause N:M  N 秒时暂停，M 秒时继续
#include "mainwindow.h"
#include <QApplication>
#include <QSurfaceFormat>
#include <QTimer>
#include <cstdio>
#include <sys/resource.h>

static double cpuSeconds() {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_utime.tv_sec + ru.ru_stime.tv_sec + (ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1e6;
}

int main(int argc, char *argv[]) {
    // macOS 上要明确申请 3.3 以上的 Core Profile，才能用 #version 330 的着色器
    QSurfaceFormat fmt;
    fmt.setVersion(4, 1);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    Options opt;
    QString url;
    QStringList args = app.arguments();
    struct Timed { double at; QString what, arg; };
    QList<Timed> timed;
    for (int i = 1; i < args.size(); i++) {
        const QString &a = args[i];
        auto next = [&] { return i + 1 < args.size() ? args[++i] : QString(); };
        if (a == "--render") opt.mode = next() == "image" ? VideoWidget::Image : VideoWidget::GL;
        else if (a == "--mute") opt.mute = true;
        else if (a == "--barcode") opt.barcode = true;
        else if (a == "--quit") timed.append({next().toDouble(), "quit", ""});
        else if (a.startsWith("--")) {
            QString v = next();
            timed.append({v.section(':', 0, 0).toDouble(), a.mid(2), v.section(':', 1)});
        } else url = a;
    }

    MainWindow w(opt);
    w.show();
    if (!url.isEmpty()) w.openUrl(url);
    for (const Timed &t : timed) {
        QTimer::singleShot((int)(t.at * 1000), &w, [&w, t] {
            if (t.what == "quit") {
                printf("%s；CPU 时间 %.2f 秒\n", qPrintable(w.stats()), cpuSeconds());
                fflush(stdout);
                qApp->quit();
            } else if (t.what == "grab") {
                w.grab().save(t.arg);
            } else if (t.what == "seek") {
                w.seek(t.arg.toDouble());
            } else if (t.what == "pause") {
                w.togglePause();
                QTimer::singleShot((int)((t.arg.toDouble() - t.at) * 1000), &w, [&w] { w.togglePause(); });
            }
        });
    }
    return app.exec();
}
```

</details>

```bash
./build/qplayer --mute --seek 2:7 --pause 4:5 --quit 7 bbb-10s.mp4
```

```text
显示 113 帧，丢弃 0 帧，重绘 113 次、每次 2.93 ms，声音欠载 1 次，音画偏差 中位 -0.3 ms / 绝对值最大 5.7 ms；CPU 时间 1.98 秒
```

跳转、暂停之后，同步依然准确。

## 6. 播放网络流

`openUrl` 接受任何 FFmpeg 能打开的地址。用第 5 章的推流程序推一路带时间条码的流到 MediaMTX，再让 `qplayer` 播放它的各种协议：

```bash
./build/qplayer --mute --barcode --quit 15 rtmp://127.0.0.1/live/test
```

| 协议 | 显示 / 丢弃 | 延迟（中位数） |
|---|---|---|
| RTMP | 328 / 0 帧 | 15 ms |
| RTSP | 351 / 0 帧 | 50 ms |
| HLS（MediaMTX 的 LL-HLS，FFmpeg 按普通 HLS 读） | 224 / 88 帧 | 1021 ms |

RTMP、RTSP 的结果和第 5 章 C 程序测的一致，RTSP 同样多了一帧。HLS 的延迟比第 5 章按 PTS 匀速播放测得的 3.3 秒小得多，代价是丢了 88 帧：FFmpeg 一次下载、解码一整个切片，帧是成批到来的，`qplayer` 发现落后就丢帧追上去，所以画面会跳。这正是直播播放器常见的取舍：要么忍受延迟，要么丢帧追赶。

## 7. 小结

- QAudioSink 的拉模式：自己实现 QIODevice 的 `readData`，声卡按自己的节奏来取数据。
- 拉模式下 `bytesFree()` 永远是 0，声卡又成块地取数据，用缓冲区的数据量算声音时钟会一跳一跳的，导致大量丢帧。
- 用 `processedUSecs()` 加上「字节位置→时间戳」的对照表算声音时钟：240 帧一帧不丢，偏差中位数 −0.3 毫秒。
- 打开和跳转后让声音等第一帧画面：偏差最大值从 64 毫秒降到 5.7 毫秒，跳转不再丢帧；只有声音的文件不要等。
- 没有声音时用系统时钟；暂停时声音时钟自然停止，系统时钟要挪起点。
- 给 GUI 程序加上定时动作的命令行参数，就能自动、可重复地测试。

## 练习

1. 第 3 节的对照表中，`readData` 每次交出的数据都记一段。如果声卡每 85 毫秒取一次，播放一个小时会积累多少段？程序会不会因此越来越慢？
2. 拖动进度条时，`m_dragging` 为真，`updatePosition` 就不更新滑块的位置。如果不这样处理，会有什么现象？
3. 第 6 节的 HLS 丢了 88 帧，延迟只有 1 秒。如果希望 HLS 播放不跳帧，应该怎么改？延迟会变成多少？

<details>
<summary>点击查看答案</summary>

1. 一小时是 3600 / 0.0853 ≈ 42000 次 `readData`，但对照表不会积累：`clock()` 每次查询时，都会把已经播完的段（结束位置不超过已播放的位置）删掉，只记下最后一段的结束时间戳。表中只剩声卡缓冲区里还没播的那几段，0.25 秒的缓冲区最多也就三四段。界面线程每 5 毫秒查询一次，删除得很及时。用 `QVector::removeFirst()` 删除队头需要移动后面的元素，元素这么少，开销可以忽略。

2. 用户按住滑块拖动时，界面线程每显示一帧都会调用 `setValue` 把滑块拉回当前的播放位置，滑块就会在用户的手指和播放位置之间来回跳，几乎拖不动。所以 `sliderPressed` 时设置 `m_dragging = true`，暂停更新；`sliderReleased` 时清除标志，并按滑块的位置跳转。

3. 去掉直播追帧，像第 5 章的 `latency_probe` 加 `play=1` 那样：第一帧马上显示，之后严格按 PTS 的间隔播放，帧来晚了就等（播放时钟往后推），而不是丢掉。这样画面是连续的，延迟则回到第 5 章测得的约 3.3 秒（FFmpeg 从倒数第 3 个 1 秒的切片开始播）。更好的办法是让播放器支持 LL-HLS 的部分切片，或者干脆换成更适合低延迟的协议。实际的播放器往往两者兼顾：缓冲足够时按正常速度播放，落后太多时用 1.1 倍左右的速度「快放」追上去，而不是直接丢帧。

</details>
