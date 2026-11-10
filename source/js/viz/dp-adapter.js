/* 设计模式专栏《适配器》：一次调用怎样经过适配器翻译成 FFmpeg 的 C API；提前返回时资源有没有释放（dp-adapter） */
(function () {
  'use strict';
  var COL = { caller: 20, adapter: 270, c: 520 };

  function view(m, x) {
    x = x || {};
    var hl = x.hl || {}, boxes = [], arrows = [], texts = [];
    texts.push({ id: 'h1', x: COL.caller, y: 20, text: '播放器（只认识 MediaSource）', cls: 'muted' });
    if (m.useAdapter) texts.push({ id: 'h2', x: COL.adapter, y: 20, text: '适配器 FfmpegSource', cls: 'muted' });
    texts.push({ id: 'h3', x: COL.c, y: 20, text: 'FFmpeg 的 C API', cls: 'muted' });
    boxes.push({ id: 'caller', x: COL.caller, y: 34, w: 200, h: 40, text: m.callerText, hl: hl.caller });
    if (m.useAdapter) boxes.push({ id: 'adapter', x: COL.adapter, y: 34, w: 200, h: 40, text: m.adapterText, hl: hl.adapter });
    boxes.push({ id: 'capi', x: COL.c, y: 34, w: 200, h: 40, text: m.cText, hl: hl.capi });
    texts.push({ id: 'h4', x: COL.c, y: 104, text: '堆上的资源', cls: 'muted' });
    boxes.push({ id: 'fmt', x: COL.c, y: 114, w: 200, h: 32, text: 'AVFormatContext', sub: m.fmt ? '' : '', hl: m.fmt === 'open' ? 'new' : m.fmt === 'leak' ? 'warn' : 'empty' });
    boxes.push({ id: 'pkt', x: COL.c, y: 160, w: 200, h: 32, text: 'AVPacket', hl: m.pkt === 'open' ? 'new' : m.pkt === 'leak' ? 'warn' : 'empty' });
    (x.arrows || []).forEach(function (a) { arrows.push({ from: a[0], to: a[1], hl: 'cur', label: a[2], pkt: a[3] || null }); });
    texts.push({ id: 'leak', x: COL.caller, y: 130, text: '泄漏：' + (m.leaks ? m.leaks + ' 次调用，约 ' + (m.leaks * 510) + ' KB' : '0'), cls: m.leaks ? null : 'muted' });
    if (x.note) texts.push({ id: 'note', x: COL.caller, y: 222, text: x.note, cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts };
  }

  function reset(m, useAdapter) {
    m.useAdapter = useAdapter; m.fmt = null; m.pkt = null;
    m.callerText = 'keyframeAfter(path, 5)'; m.adapterText = ''; m.cText = '';
  }

  function adapterRun(early, m, rec) {
    reset(m, true);
    rec.step(1, '播放器调用 FfmpegSource::open(path)。它只认识 MediaSource 接口，不知道背后是 FFmpeg。');
    m.adapterText = 'open()'; m.cText = 'avformat_open_input()'; m.fmt = 'open';
    rec.step(2, '适配器把调用翻译成 avformat_open_input，拿到的 AVFormatContext* 立刻放进 unique_ptr（自定义删除器调用 avformat_close_input）。',
      { hl: { adapter: 'cur', capi: 'cur' }, arrows: [['caller', 'adapter', 'open'], ['adapter', 'capi', '翻译']] });
    m.cText = 'av_packet_alloc()'; m.pkt = 'open';
    rec.step(3, '再分配一个复用的 AVPacket，同样交给 unique_ptr 管理。', { hl: { adapter: 'cur', capi: 'cur' } });
    m.callerText = 'src->next()'; m.adapterText = 'next()'; m.cText = 'av_read_frame() → 0';
    rec.step(4, '播放器调用 next()。适配器调用 av_read_frame：返回值小于 0 表示结束，翻译成 std::nullopt；成功就把数据复制进一个 Packet，再 av_packet_unref。',
      { hl: { caller: 'cur', adapter: 'cur', capi: 'cur' }, arrows: [['caller', 'adapter', 'next()'], ['adapter', 'capi', 'av_read_frame']] });
    rec.step(4, '把 Packet 返回给播放器：错误码、手动释放这些 C 的细节，都没有越过适配器。', { arrows: [['adapter', 'caller', '', 'Packet']], hl: { caller: 'ok' } });
    if (early) {
      m.callerText = '找到了：return 5.46';
      rec.step(5, '播放器找到了 5 秒后的第一个关键帧，直接 return。它没有、也不需要做任何清理。', { hl: { caller: 'cur' } });
      m.adapterText = '~FfmpegSource()'; m.cText = 'av_packet_free / avformat_close_input'; m.fmt = null; m.pkt = null;
      rec.step(6, 'src 离开作用域，适配器析构：两个 unique_ptr 依次调用 av_packet_free 和 avformat_close_input。实测 10 次调用，leaks 报告 0 处泄漏。',
        { hl: { adapter: 'ok', capi: 'ok' }, arrows: [['adapter', 'capi', '自动释放']], note: 'RAII：资源的释放跟着对象的生命周期走，不管从哪条路径返回' });
    }
  }

  function rawRun(m, rec) {
    reset(m, false);
    var before = m.leaks || 0;
    rec.step(1, '不用适配器：播放器的代码直接调用 FFmpeg 的 C API。');
    m.cText = 'avformat_open_input()'; m.fmt = 'open';
    rec.step(2, 'avformat_open_input 打开文件，堆上分配了 AVFormatContext 和它的各种缓冲区。', { hl: { capi: 'cur' }, arrows: [['caller', 'capi', '直接调用']] });
    m.cText = 'av_packet_alloc()'; m.pkt = 'open';
    rec.step(3, 'av_packet_alloc 分配一个 AVPacket。这两样都要求调用者在每一条返回路径上手动释放。', { hl: { capi: 'cur' } });
    m.cText = 'av_read_frame() → 0';
    rec.step(4, '循环读包、av_packet_unref……', { hl: { capi: 'cur' }, arrows: [['caller', 'capi', 'av_read_frame']] });
    m.callerText = '找到了：return 5.46'; m.fmt = 'leak'; m.pkt = 'leak'; m.leaks = before + 1;
    rec.step(5, '找到关键帧，直接 return。释放的代码写在循环后面，这条路径没有经过它：AVFormatContext 和 AVPacket 都泄漏了。',
      { hl: { caller: 'warn' }, note: '实测：调用 10 次，leaks 报告 820 处泄漏，共 5226720 字节（约 5 MB）' });
  }

  DSViz.register('dp-adapter', {
    title: '经过适配器的一次调用，以及提前返回时的资源',
    width: 740,
    height: 236,
    code: [
      'auto src = FfmpegSource::open(path, err);   // 适配器版本',
      '    // 内部：avformat_open_input → unique_ptr',
      '    // 内部：av_packet_alloc → unique_ptr',
      'while (auto pkt = src->next())              // 内部：av_read_frame / av_packet_unref',
      '    if (pkt->video && pkt->keyframe && pkt->time >= sec) return pkt->time;',
      '}   // src 析构：av_packet_free、avformat_close_input'
    ],
    init: function () { var m = { leaks: 0 }; reset(m, true); return m; },
    view: view,
    start: function (m, rec) { adapterRun(true, m, rec); },
    actions: [
      { label: '适配器：提前返回', run: function (m, p, rec) { adapterRun(true, m, rec); } },
      { label: '直接用 C API：提前返回', run: function (m, p, rec) { rawRun(m, rec); } },
      { label: '清零泄漏计数', run: function (m, p, rec) { m.leaks = 0; reset(m, true); rec.step(1, '泄漏计数已清零。'); } }
    ]
  });
})();
