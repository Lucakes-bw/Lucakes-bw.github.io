/* 设计模式专栏《抽象工厂》：解码器和渲染器分别配置 vs 由一个工厂整套创建（dp-abfactory） */
(function () {
  'use strict';
  var FAM = [
    { id: 'sw', name: '软件', dec: 'FFmpeg 软解', ren: 'SDL 渲染', mem: '内存' },
    { id: 'vt', name: 'VideoToolbox', dec: 'VideoToolbox 硬解', ren: 'Metal 渲染', mem: 'GPU（VT）' },
    { id: 'va', name: 'VA-API', dec: 'VA-API 硬解', ren: 'EGL 渲染', mem: 'GPU（VA）' }
  ];
  var ROW = { sw: 40, vt: 100, va: 160 };

  function view(m, x) {
    x = x || {};
    var hl = x.hl || {}, boxes = [], arrows = [], texts = [];
    texts.push({ id: 'hdec', x: 160, y: 24, text: '解码器', cls: 'muted' });
    texts.push({ id: 'hren', x: 470, y: 24, text: '渲染器', cls: 'muted' });
    FAM.forEach(function (f) {
      texts.push({ id: 'n' + f.id, x: 10, y: ROW[f.id] + 24, text: f.name + ' 族' });
      boxes.push({ id: 'd' + f.id, x: 160, y: ROW[f.id], w: 180, h: 36, text: f.dec, hl: hl['d' + f.id] || (x.dimAll ? 'dim' : null) });
      boxes.push({ id: 'r' + f.id, x: 470, y: ROW[f.id], w: 180, h: 36, text: f.ren, hl: hl['r' + f.id] || (x.dimAll ? 'dim' : null) });
    });
    if (x.link) arrows.push({ from: 'd' + x.link[0], to: 'r' + x.link[1], hl: x.link[2], label: x.link[3], pkt: x.link[4] || null });
    if (x.result) texts.push({ id: 'res', x: 10, y: 224, text: x.result });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts };
  }

  function separate(m, p, rec) {
    rec.step(1, '没有抽象工厂：配置文件里有两个独立的选项，decoder 和 renderer，分别交给两个简单工厂。');
    rec.step(2, '用户为了省电，把 decoder 改成了 videotoolbox（硬解）。', { hl: { dvt: 'cur' }, dimAll: true });
    rec.step(3, '但 renderer 还是默认的 sdl。两个工厂各做各的，谁也不知道对方选了什么。', { hl: { dvt: 'cur', rsw: 'cur' }, dimAll: true });
    rec.step(4, '解码出来的帧留在 GPU 上，SDL 渲染器只能画内存里的帧：画不出来。3 × 3 = 9 种组合里，只有对角线上的 3 种能用。',
      { hl: { dvt: 'warn', rsw: 'warn' }, dimAll: true, link: ['vt', 'sw', 'warn', '帧在 GPU 上', 'GPU 帧'], result: '✗ FFmpeg 里同样的错误：Impossible to convert between the formats …' });
  }

  function factory(envName, env, m, rec) {
    rec.step(5, '用抽象工厂：程序启动时按运行环境（' + envName + '）挑一个后端工厂，之后解码器和渲染器都向它要。');
    var chosen = null;
    [['vt', env.vt], ['va', env.va], ['sw', true]].forEach(function (c) {
      if (chosen) return;
      var hl = {};
      hl['d' + c[0]] = c[1] ? 'ok' : 'warn'; hl['r' + c[0]] = c[1] ? 'ok' : 'warn';
      rec.step(6, FAM.filter(function (f) { return f.id === c[0]; })[0].name + ' 后端可用吗？' + (c[1] ? '可用，就用这一族。' : '不可用，看下一个。'), { hl: hl, dimAll: true });
      if (c[1]) chosen = c[0];
    });
    var hl = {}; hl['d' + chosen] = 'ok'; hl['r' + chosen] = 'ok';
    var f = FAM.filter(function (x) { return x.id === chosen; })[0];
    rec.step(7, '解码器和渲染器都由「' + f.name + '」工厂创建，一定配套：帧在' + f.mem + '，渲染器正好能画。',
      { hl: hl, dimAll: true, link: [chosen, chosen, 'ok', '帧在' + f.mem, '帧'], result: '✓ 240 / 240 帧' });
  }

  DSViz.register('dp-abfactory', {
    title: '解码器和渲染器：分别配置 vs 抽象工厂',
    width: 740,
    height: 236,
    code: [
      'auto dec = makeDecoder(cfg["decoder"]);     // 分别配置',
      '                                         // decoder = videotoolbox',
      'auto ren = makeRenderer(cfg["renderer"]);   // renderer = sdl（默认）',
      'ren->draw(dec->decode(i));                  // ✗ 帧格式不匹配',
      'const MediaBackend& b = chooseBackend(env);  // 抽象工厂',
      '// 按优先级找第一个 available() 的后端',
      'auto dec = b.createDecoder(); auto ren = b.createRenderer();   // 出自同一个工厂'
    ],
    init: function () { return {}; },
    view: view,
    start: function (m, rec) { separate(m, {}, rec); },
    actions: [
      { label: '分别配置', run: separate },
      { label: '抽象工厂：MacBook', run: function (m, p, rec) { factory('MacBook', { vt: true, va: false }, m, rec); } },
      { label: '抽象工厂：Linux 核显', run: function (m, p, rec) { factory('Linux 台式机', { vt: false, va: true }, m, rec); } },
      { label: '抽象工厂：无 GPU 服务器', run: function (m, p, rec) { factory('没有 GPU 的服务器', { vt: false, va: false }, m, rec); } }
    ]
  });
})();
