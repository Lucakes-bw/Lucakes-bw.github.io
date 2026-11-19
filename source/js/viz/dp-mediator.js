/* 设计模式专栏《中介者》：导出对话框的星形协调——每个控件只通知 ExportDialog，由 ExportDialog 统一更新所有关联控件（dp-mediator） */
(function () {
  'use strict';

  function initState(m) {
    m.fmt = 'mp4';
    m.vEn = true; m.vCodec = 'h264'; m.crfEn = true; m.w = 1280; m.h = 720;
    m.aEn = true; m.aCodec = 'aac';
    m.clip = false; m.s = 0; m.e = 10; m.note = '完整视频';
    m.btnEn = true; m.hint = '就绪';
  }

  function view(m, x) {
    x = x || {};
    var hl = x.hl || {}, boxes = [], arrows = [], texts = [];
    texts.push({ id: 'tl', x: 20, y: 22, text: '同事控件（彼此不持有指针）', cls: 'muted' });
    texts.push({ id: 'tm', x: 295, y: 22, text: '中介者', cls: 'muted' });
    texts.push({ id: 'tr', x: 500, y: 22, text: '被中介者联动更新的控件', cls: 'muted' });

    boxes.push({
      id: 'fmt', x: 20, y: 42, w: 200, h: 48,
      text: 'FormatSelect: ' + m.fmt,
      sub: '容器格式 mp4 / gif / wav', hl: hl.fmt
    });
    boxes.push({
      id: 'video', x: 20, y: 112, w: 200, h: 54,
      text: 'VideoPanel' + (m.vEn ? '（开）' : '（禁用）'),
      sub: 'codec=' + m.vCodec + ' crf=' + (m.crfEn ? '开' : '关') + ' ' + m.w + '×' + m.h,
      hl: hl.video || (m.vEn ? null : 'dim')
    });
    boxes.push({
      id: 'range', x: 20, y: 188, w: 200, h: 52,
      text: 'RangePanel' + (m.clip ? '（' + m.s + '~' + m.e + 's）' : '（全程）'),
      sub: '提示：' + m.note, hl: hl.range
    });

    boxes.push({
      id: 'med', x: 280, y: 108, w: 165, h: 64,
      text: 'ExportDialog',
      sub: 'changed(sender)', hl: hl.med
    });

    boxes.push({
      id: 'audio', x: 500, y: 58, w: 215, h: 50,
      text: 'AudioPanel' + (m.aEn ? '（开）' : '（禁用）'),
      sub: 'codec=' + m.aCodec, hl: hl.audio || (m.aEn ? null : 'dim')
    });
    boxes.push({
      id: 'btn', x: 500, y: 158, w: 215, h: 54,
      text: 'ExportButton' + (m.btnEn ? '（可点击）' : '（已禁用）'),
      sub: '提示：' + m.hint, hl: hl.btn
    });

    if (x.from) arrows.push({ from: x.from, to: 'med', hl: 'cur', label: 'notify()' });
    (x.to || []).forEach(function (t) {
      arrows.push({ from: 'med', to: t, hl: hl[t] || 'new' });
    });
    if (x.note) texts.push({ id: 'note', x: 20, y: 268, text: x.note, cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts };
  }

  function toGif(m, p, rec) {
    initState(m);
    rec.step(1, '初始状态：格式为 mp4，视频 h264（CRF 开启），音频 aac，导出按钮可点。', { hl: { med: 'cur' } });
    m.fmt = 'gif';
    rec.step(1, '用户把 FormatSelect 切到 "gif"：FormatSelect 只修改自己的 fmt，然后调用 notify() 通知中介者 ExportDialog。它不知道有哪些面板要跟着变。', {
      from: 'fmt', hl: { fmt: 'cur', med: 'cur' }
    });
    m.vEn = true; m.vCodec = 'gif'; m.crfEn = false; m.aEn = false;
    rec.step(4, 'ExportDialog::changed() 执行格式联动规则：把 VideoPanel 的编码锁为 gif、关掉 CRF，把 AudioPanel 整个禁用。', {
      to: ['video', 'audio'], hl: { med: 'cur', video: 'new', audio: 'new' }
    });
    m.btnEn = true; m.hint = '就绪';
    rec.step(6, 'ExportDialog 接着检查参数合法性：当前未开启超长片段，分辨率合法，ExportButton 保持可点击。', {
      to: ['btn'], hl: { med: 'cur', btn: 'ok' },
      note: '星形拓扑：FormatSelect → ExportDialog → VideoPanel / AudioPanel / ExportButton'
    });
  }

  function copyClip(m, p, rec) {
    initState(m);
    m.clip = true; m.s = 2; m.e = 8; m.note = '精确裁剪';
    rec.step(1, '用户先在 RangePanel 勾选「只导出 2~8s 片段」：当前视频是 h264 重编码，中介者把 RangePanel 的提示设为「精确裁剪」。', {
      from: 'range', to: ['range', 'btn'], hl: { range: 'ok', med: 'cur', btn: 'ok' }
    });
    m.vCodec = 'copy';
    rec.step(2, '接着用户在 VideoPanel 把视频编码切到 "copy"（流拷贝）：VideoPanel 只管改自己的 codec 并调 notify()。', {
      from: 'video', hl: { video: 'cur', med: 'cur' }
    });
    m.crfEn = false; m.note = '流拷贝只能从关键帧切';
    rec.step(5, 'ExportDialog::changed() 重新跑一遍统一规则：不仅关掉 VideoPanel 的 CRF，还顺带把 RangePanel 的提示更新为「流拷贝只能从关键帧切」——网状版漏掉的正是这一步反向联动。', {
      to: ['video', 'range', 'btn'], hl: { med: 'cur', video: 'new', range: 'ok', btn: 'ok' },
      note: '无论先勾片段再切 copy，还是先切 copy 再勾片段，中介者都跑同一遍 changed()，结果天然一致'
    });
  }

  function oddSize(m, p, rec) {
    initState(m);
    m.w = 641; m.h = 360;
    rec.step(2, '用户在 VideoPanel 把分辨率改成 641×360（奇数宽），VideoPanel 调用 notify()。', {
      from: 'video', hl: { video: 'cur', med: 'cur' }
    });
    m.btnEn = false; m.hint = 'h264 编码要求宽高都是偶数';
    rec.step(6, 'ExportDialog::changed() 检查发现：mp4 + h264 重编码要求宽高都是偶数（YUV420 色度抽样），于是禁用 ExportButton 并写明原因。', {
      to: ['btn'], hl: { med: 'cur', btn: 'warn' }
    });
    m.vCodec = 'copy'; m.crfEn = false; m.btnEn = true; m.hint = '就绪';
    rec.step(4, '用户再把编码改成 "copy"（不重新编码）：ExportDialog 重新评估，流拷贝不经过 h264 编码器，ExportButton 自动恢复可点。', {
      from: 'video', to: ['video', 'btn'], hl: { med: 'cur', video: 'new', btn: 'ok' }
    });
  }

  function gifTooLong(m, p, rec) {
    initState(m);
    m.fmt = 'gif'; m.vCodec = 'gif'; m.crfEn = false; m.aEn = false;
    m.clip = true; m.s = 0; m.e = 20;
    rec.step(1, '当前格式为 gif，用户在 RangePanel 设了 0~20s 的片段，RangePanel 调用 notify()。', {
      from: 'range', hl: { range: 'cur', med: 'cur' }
    });
    m.btnEn = false; m.hint = 'GIF 片段不能超过 15 秒';
    rec.step(7, 'ExportDialog::changed() 综合 FormatSelect(gif) 和 RangePanel(20s) 两个控件的状态，禁用 ExportButton 并提示「GIF 片段不能超过 15 秒」。', {
      to: ['btn'], hl: { med: 'cur', fmt: 'cmp', range: 'cmp', btn: 'warn' },
      note: '跨多个控件的联合约束写在 ExportDialog::changed() 一处，不用拆到各个控件里'
    });
  }

  DSViz.register('dp-mediator', {
    title: '中介者：控件只向 ExportDialog 报告变化，联动规则集中在一处',
    width: 735,
    height: 284,
    code: [
      'void FormatSelect::choose(string f) { fmt = f; notify(); }   // 控件只改自己 + notify()',
      'void VideoPanel::chooseCodec(string c) { codec = c; notify(); }',
      'void ExportDialog::changed(Widget*) {                        // 唯一知道所有控件的地方',
      '  // 1. 按 format.fmt 联动 video / audio 的开关、编码器和 crfEnabled',
      '  // 2. 按 range.clipOnly 和 video.codec 更新 range.note',
      '  // 3. 检查偶数分辨率、起止时间',
      '  // 4. 检查 GIF 片段时长 ≤ 15 秒，更新 btn.enabled 和 btn.hint',
      '}'
    ],
    init: function () { var m = {}; initState(m); return m; },
    view: view,
    start: function (m, rec) { toGif(m, {}, rec); },
    actions: [
      { label: '格式切到 GIF', run: toGif },
      { label: '先选片段再切 copy', run: copyClip },
      { label: '奇数分辨率 641×360', run: oddSize },
      { label: 'GIF 选 20 秒片段', run: gifTooLong }
    ]
  });
})();
