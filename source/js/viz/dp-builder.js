/* 设计模式专栏《建造者》：链式调用一步步填写草稿，build() 统一检查（dp-builder） */
(function () {
  'use strict';
  var FIELDS = ['input', 'output', 'codec', 'preset', 'crf', 'bitrate', 'size', 'audio'];
  var DEFAULTS = { codec: 'libx264（默认）', preset: 'medium（默认）', audio: 'aac（默认）' };
  var CHECKS = [
    ['in', '有输入文件'], ['out', '有输出文件'], ['excl', 'crf 和码率只设了一个'], ['range', 'crf 在 0～51 之间'], ['even', '宽和高都是偶数']
  ];

  function view(m, x) {
    x = x || {};
    var boxes = [], texts = [];
    texts.push({ id: 'tf', x: 20, y: 20, text: '建造者内部的草稿', cls: 'muted' });
    FIELDS.forEach(function (f, i) {
      var v = m.draft[f];
      boxes.push({ id: 'f' + f, x: 20, y: 30 + i * 28, w: 320, h: 24, text: f + ' = ' + (v == null ? (DEFAULTS[f] || '（未设置）') : v),
        hl: x.cur === f ? 'new' : (v == null && !DEFAULTS[f] ? 'empty' : null) });
    });
    texts.push({ id: 'tc', x: 390, y: 20, text: 'build() 的检查', cls: 'muted' });
    CHECKS.forEach(function (c, i) {
      var st = x.checks && x.checks[c[0]];
      boxes.push({ id: 'c' + c[0], x: 390, y: 30 + i * 32, w: 330, h: 26, text: (st === true ? '✓ ' : st === false ? '✗ ' : '') + c[1],
        hl: st === true ? 'ok' : st === false ? 'warn' : 'empty' });
    });
    if (x.result) boxes.push({ id: 'res', x: 390, y: 200, w: 330, h: 40, text: x.result, hl: x.ok ? 'ok' : 'warn' });
    return { kind: 'free', boxes: boxes, texts: texts };
  }

  function run(steps, m, rec) {
    m.draft = {};
    rec.step(1, 'EncodeJobBuilder() 先创建一个空的草稿：codec、preset、音频有默认值，其余都没设置。');
    steps.forEach(function (s) {
      m.draft[s[0]] = s[1];
      rec.step(s[2], '.' + s[3] + '：把 ' + s[0] + ' 写进草稿，返回建造者自己的引用，所以可以接着点下一个。', { cur: s[0] });
    });
    var d = m.draft, checks = {}, ok = true, why = '';
    var crf = d.crf == null ? null : +d.crf;
    var w = d.size ? +d.size.split('×')[0] : 0, h = d.size ? +d.size.split('×')[1] : 0;
    var rules = {
      'in': [d.input != null, '缺少输入文件'], out: [d.output != null, '缺少输出文件'], excl: [!(d.crf != null && d.bitrate != null), 'crf 和码率只能二选一'],
      range: [crf == null || (crf >= 0 && crf <= 51), 'crf 必须在 0～51 之间'], even: [w % 2 === 0 && h % 2 === 0, 'libx264 要求宽和高都是偶数']
    };
    for (var i = 0; i < CHECKS.length; i++) {
      var k = CHECKS[i][0];
      checks[k] = rules[k][0];
      if (!rules[k][0]) { ok = false; why = rules[k][1]; }
      rec.step(7, 'build() 检查：' + CHECKS[i][1] + '？' + (rules[k][0] ? '通过。' : '不通过：' + rules[k][1] + '。'), { checks: Object.assign({}, checks) });
      if (!ok) break;
    }
    rec.step(7, ok ? '全部检查通过，build() 返回一个完整、合法的 EncodeJob。之后它不会再被修改。' : '检查不通过，build() 不返回任务，而是返回错误原因。错误在执行 ffmpeg 之前就被发现了。',
      { checks: checks, ok: ok, result: ok ? '✓ 得到 EncodeJob' : '✗ ' + why });
  }

  var NORMAL = [['input', 'bbb-10s.mp4', 2, 'input("bbb-10s.mp4")'], ['output', 'preview.mp4', 3, 'output("preview.mp4")'],
                ['crf', '28', 4, 'crf(28)'], ['preset', 'veryfast', 5, 'preset("veryfast")'], ['size', '640×360', 6, 'size(640, 360)']];

  DSViz.register('dp-builder', {
    title: '建造者：一步步填写草稿，build() 统一检查',
    width: 740,
    height: 260,
    code: [
      'EncodeJobBuilder()',
      '    .input("bbb-10s.mp4")',
      '    .output("preview.mp4")',
      '    .crf(28)                 // 有的情况下还会调用 .bitrate(800)',
      '    .preset("veryfast")',
      '    .size(640, 360)          // 或者 .size(641, 360)',
      '    .build();'
    ],
    init: function () { return { draft: {} }; },
    view: view,
    start: function (m, rec) { run(NORMAL, m, rec); },
    actions: [
      { label: '正常构建', run: function (m, p, rec) { run(NORMAL, m, rec); } },
      { label: '忘了 output', run: function (m, p, rec) { run(NORMAL.filter(function (s) { return s[0] !== 'output'; }), m, rec); } },
      { label: 'crf 和码率都设了', run: function (m, p, rec) { run(NORMAL.slice(0, 3).concat([['bitrate', '800k', 4, 'bitrate(800)']]), m, rec); } },
      { label: '宽度 641', run: function (m, p, rec) { run(NORMAL.slice(0, 4).concat([['size', '641×360', 6, 'size(641, 360)']]), m, rec); } }
    ]
  });
})();
