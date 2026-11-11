/* 设计模式专栏《模板方法》：基类的 run() 按固定顺序走流程，到了可变的步骤就调子类（好莱坞原则）；对照「重写时必须调基类」的写法（dp-template-method） */
(function () {
  'use strict';

  var STEPS = [
    { id: 's1', text: '打开输入、选流' },
    { id: 's2', text: '打开解码器和编码器' },
    { id: 's3', text: '读包、解码（循环）' },
    { id: 's4', text: '处理这一帧' },
    { id: 's5', text: '编码、写文件' },
    { id: 's6', text: '冲刷、关闭文件' }
  ];

  function view(m, x) {
    x = x || {};
    var hl = x.hl || {}, boxes = [], arrows = [], texts = [];
    texts.push({ id: 'tb', x: 20, y: 22, text: m.callSuper ? '基类 ExportJob（步骤是虚函数）' : '基类 ExportJob::run()：流程，非虚', cls: 'muted' });
    texts.push({ id: 'tc', x: 420, y: 22, text: '子类 ' + m.cls + '：只写不同的步骤', cls: 'muted' });
    STEPS.forEach(function (s, i) {
      boxes.push({ id: s.id, x: 20, y: 34 + i * 44, w: 280, h: 36, text: (i + 1) + '. ' + s.text, hl: hl[s.id] });
    });
    m.methods.forEach(function (d) {
      boxes.push({ id: d.id, x: 420, y: 34 + d.row * 44, w: 300, h: 36, text: d.text, hl: hl[d.id] });
      if (d.from) arrows.push({ from: d.from, to: d.id, hl: hl['a' + d.id] || (hl[d.id] ? 'cur' : null) });
    });
    texts.push({ id: 'st', x: 20, y: 314, text: '已编码 ' + m.frames + ' 帧    打开着的文件 ' + m.open + ' 个', cls: 'muted' });
    if (x.note) texts.push({ id: 'note', x: 20, y: 338, text: x.note, cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts };
  }

  function setup(m, cls, methods, callSuper) {
    m.cls = cls; m.methods = methods; m.callSuper = !!callSuper; m.frames = 0; m.open = 0; m.done = false;
  }

  // 三个子类：哪些步骤重写了、哪些用基类的默认版本
  function job(name, media, codec, proc, frames) {
    return function (m, p, rec) {
      setup(m, name, [
        { id: 'm1', row: 0, from: 's1', text: 'media() → ' + media },
        { id: 'm2', row: 1, from: 's2', text: 'codec() → ' + codec },
        { id: 'm4', row: 3, from: 's4', text: proc ? 'process(f)：' + proc : 'process(f)：没重写，用默认版本' }
      ]);
      rec.step(1, '客户只调用 job->run()。run() 是公有的非虚函数，子类改不了流程，也不用知道流程。');
      m.open = 1;
      rec.step(2, '第 1 步是公共代码：打开 bbb-10s.mp4。中途要选哪一路流，基类调子类的 media()：' + name + ' 回答 ' + media + '。', { hl: { s1: 'cur', m1: 'new' } });
      m.open = 2;
      rec.step(3, '第 2 步：打开解码器，再向子类要编码器的名字 codec()，得到 ' + codec + '，打开输出文件。', { hl: { s2: 'cur', m2: 'new' } });
      rec.step(4, '第 3 步：读包、解码，这个循环三个任务完全一样，只写在基类里。', { hl: { s3: 'cur' } });
      rec.step(5, '每解码出一帧，基类调钩子 process(f)。' + (proc ? name + ' 重写了它：奇数帧丢掉，留下的缩到 480×270。' : name + ' 没有重写，调到的是基类的默认版本：什么都不做。'),
        { hl: { s4: 'cur', m4: proc ? 'new' : 'cmp' } });
      m.frames = frames;
      rec.step(6, '第 5 步：编码、写文件。循环结束时一共编码了 ' + frames + ' 帧。', { hl: { s5: 'cur' } });
      m.open = 0; m.done = true;
      rec.step(7, '第 6 步：冲刷解码器和编码器、关闭两个文件，都由基类来做，子类没有机会忘。', { hl: { s6: 'ok' },
        note: '好莱坞原则：子类从不调用基类的流程，都是基类在合适的时候调子类' });
    };
  }

  function callSuper(m, p, rec) {
    setup(m, 'GifExport', [
      { id: 'c1', row: 0, from: 's1', text: 'prepare()：调基类 + 开调色板' },
      { id: 'c6', row: 5, from: 's6', text: 'cleanup()：只关调色板，漏调基类' }
    ], true);
    rec.step(8, '对照：另一种写法把 prepare()、cleanup() 做成虚函数，约定「子类重写时必须先调基类的版本」。');
    m.open = 3;
    rec.step(9, 'GifExport::prepare() 记得调了 ExportJob::prepare()（打开输入和输出），再打开调色板临时文件：一共 3 个。', { hl: { s1: 'cur', c1: 'new' } });
    m.frames = 120;
    rec.step(10, '中间的读包、解码、编码照常进行。', { hl: { s3: 'cur', s4: 'cur', s5: 'cur' } });
    m.open = 2; m.done = true;
    rec.step(11, 'cleanup() 被子类整个替换了。它只关了调色板，忘了调 ExportJob::cleanup()，输入和输出两个文件一直开着。编译器不会提醒。',
      { hl: { s6: 'warn', c6: 'warn' }, note: '每个任务漏 2 个文件描述符：上限 256 时，批量导出到第 127 个就失败' });
  }

  DSViz.register('dp-template-method', {
    title: '模板方法：基类掌握流程，到了可变的步骤才调子类',
    width: 740,
    height: 350,
    code: [
      'job->run("bbb-10s.mp4", out);              // 公有、非虚',
      '  if (!dm.open(in)) ...; Decoder dec(media());',
      '  Encoder enc{codec()}; w.open(out);',
      '  while (dm.read(pkt)) if (dec.decode(pkt, f)) ...',
      '    if (process(f))                         // 钩子',
      '      w.write(enc.encode(f));',
      '  while (dec.flush(f)) ...; w.close(); dm.close();',
      '// 对照：「重写时必须调基类」',
      'bool prepare() override { ExportJob::prepare(); palette_.open(...); }',
      '/* 读包、解码、编码 */',
      'void cleanup() override { palette_.close(); }   // 漏了 ExportJob::cleanup()'
    ],
    init: function () { var m = {}; setup(m, 'GifExport', []); return m; },
    view: view,
    start: function (m, rec) { job('GifExport', 'Video', 'gif', '隔帧丢、缩到 480×270', 120)(m, {}, rec); },
    actions: [
      { label: '导出 MP4', run: job('Mp4Export', 'Video', 'h264', null, 240) },
      { label: '导出 GIF', run: job('GifExport', 'Video', 'gif', '隔帧丢、缩到 480×270', 120) },
      { label: '导出 WAV', run: job('WavExport', 'Audio', 'pcm_s16le', null, 470) },
      { label: '漏调基类', run: callSuper }
    ]
  });
})();
