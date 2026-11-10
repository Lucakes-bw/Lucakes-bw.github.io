/* 设计模式专栏《代理》：全部解码 vs 虚代理（用到时才解码），以及缓存代理的命中和淘汰（dp-proxy） */
(function () {
  'use strict';

  var N = 6, KB = 56;   // 6 张缩略图，每张 160×90 RGBA ≈ 56 KB

  function view(m, x) {
    x = x || {};
    var hl = x.hl || {}, boxes = [], arrows = [], texts = [];
    if (m.mode === 'cache') return cacheView(m, x);
    boxes.push({ id: 'cli', x: 16, y: 120, w: 110, h: 44, text: 'Gallery', sub: m.page < 0 ? '刚打开' : '显示第 ' + (m.page + 1) + ' 屏', hl: hl.cli });
    texts.push({ id: 't1', x: 180, y: 18, text: m.mode === 'eager' ? 'items_：VideoThumb（真实对象）' : 'items_：ThumbProxy（代理）', cls: 'muted' });
    if (m.mode !== 'eager') texts.push({ id: 't2', x: 420, y: 18, text: '堆上的 VideoThumb（已解码）', cls: 'muted' });
    for (var i = 0; i < N; i++) {
      var y = 30 + i * 38;
      var onScreen = m.page >= 0 && Math.floor(i / 3) === m.page;
      var text = m.mode === 'eager' ? 'clip' + i + '：' + KB + ' KB' : 'Proxy ' + i + '：real_ ' + (m.real[i] ? '有' : '空');
      boxes.push({ id: 'p' + i, x: 180, y: y, w: 170, h: 30, text: text, hl: hl['p' + i] || (onScreen ? 'cmp' : null) });
      if (m.mode !== 'eager' && m.real[i]) {
        boxes.push({ id: 'r' + i, x: 420, y: y, w: 150, h: 30, text: 'clip' + i + '：' + KB + ' KB', hl: hl['r' + i] });
        arrows.push({ from: 'p' + i, to: 'r' + i, hl: hl['a' + i] });
      }
    }
    (x.calls || []).forEach(function (c, k) {
      arrows.push({ from: c[0], to: c[1], label: c[2], pkt: c[2], hl: c[3] || 'cur', pktDelay: k * 350 });
    });
    texts.push({ id: 'scr', x: 600, y: 60, text: m.page < 0 ? '屏幕：还没显示' : '屏幕：clip' + (m.page * 3) + '～' + (m.page * 3 + 2), cls: 'muted' });
    texts.push({ id: 'scr2', x: 600, y: 82, text: '（一屏 3 张，黄色）', cls: 'muted' });
    texts.push({ id: 'dec', x: 600, y: 130, text: '解码次数：' + m.decodes, cls: 'muted' });
    texts.push({ id: 'mem', x: 600, y: 152, text: '堆内存：' + m.decodes * KB + ' KB', cls: 'muted' });
    if (x.note) texts.push({ id: 'note', x: 16, y: 280, text: x.note, cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts };
  }

  function cacheView(m, x) {
    var hl = x.hl || {}, boxes = [], arrows = [], texts = [];
    boxes.push({ id: 'cli', x: 16, y: 60, w: 130, h: 44, text: '调用方', sub: m.req ? 'probe("' + m.req + '")' : '', hl: hl.cli });
    boxes.push({ id: 'cp', x: 260, y: 60, w: 190, h: 44, text: 'CachingProbe', sub: '容量 2，最近用过的在前', hl: hl.cp });
    boxes.push({ id: 'fp', x: 570, y: 60, w: 170, h: 44, text: 'FileProbe', sub: '打开文件读流信息（慢）', hl: hl.fp });
    texts.push({ id: 'tc', x: 260, y: 140, text: '缓存里：', cls: 'muted' });
    m.cache.forEach(function (k, i) {
      boxes.push({ id: 'c_' + k, x: 260 + i * 100, y: 152, w: 90, h: 36, text: k, hl: hl['c_' + k] });
    });
    (x.calls || []).forEach(function (c, k) {
      arrows.push({ from: c[0], to: c[1], label: c[2], pkt: c[2], hl: c[3] || 'cur', pktDelay: k * 400 });
    });
    texts.push({ id: 'st', x: 16, y: 240, text: '命中 ' + m.hits + ' 次，未命中 ' + m.misses + ' 次（真正打开文件 ' + m.misses + ' 次）', cls: 'muted' });
    if (x.note) texts.push({ id: 'note', x: 16, y: 280, text: x.note, cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts };
  }

  function reset(m, mode) {
    m.mode = mode; m.page = -1; m.decodes = 0; m.real = [];
    m.cache = []; m.hits = 0; m.misses = 0; m.req = '';
    for (var i = 0; i < N; i++) m.real.push(false);
  }

  function eager(m, p, rec) {
    reset(m, 'eager');
    var hl = {};
    for (var i = 0; i < N; i++) hl['p' + i] = 'new';
    m.decodes = N;
    rec.step(5, '不用代理：Gallery 一构造就 make_unique<VideoThumb>，6 张缩略图全部解码，屏幕上一张都还没显示。', { hl: hl });
    m.page = 0;
    rec.step(2, '显示第 1 屏，只用到 clip0～clip2。', { hl: { cli: 'cur' }, calls: [['cli', 'p0', 'draw'], ['cli', 'p1', 'draw'], ['cli', 'p2', 'draw']] });
    rec.step(2, 'clip3～clip5 也解码好了，占着内存，用户可能根本不会翻过去。真实程序里是 120 张：打开要 95 ms、6.6 MB。', { hl: { p3: 'warn', p4: 'warn', p5: 'warn' }, note: '启动慢、内存大：付出的代价和屏幕上显示多少张无关' });
  }

  function lazy(m, p, rec) {
    reset(m, 'proxy');
    var hl = {};
    for (var i = 0; i < N; i++) hl['p' + i] = 'new';
    rec.step(1, '虚代理：Gallery 里放的是 ThumbProxy，每个只记住路径，real_ 是空的。打开媒体库不解码。', { hl: hl });
    m.page = 0;
    rec.step(2, '显示第 1 屏：Gallery 对前 3 个代理调用 draw()。Gallery 不知道它们是代理，接口完全一样。', { hl: { cli: 'cur' }, calls: [['cli', 'p0', 'draw'], ['cli', 'p1', 'draw'], ['cli', 'p2', 'draw']] });
    m.real[0] = m.real[1] = m.real[2] = true; m.decodes = 3;
    rec.step(2, '代理发现 real_ 是空的，这才创建 VideoThumb、解码，再把 draw 转发给它。', { hl: { r0: 'new', r1: 'new', r2: 'new', a0: 'cur', a1: 'cur', a2: 'cur' } });
    m.page = 1;
    m.real[3] = m.real[4] = m.real[5] = true; m.decodes = 6;
    rec.step(3, '翻到第 2 屏：clip3～clip5 第一次被画，同样在这时解码。', { hl: { r3: 'new', r4: 'new', r5: 'new', a3: 'cur', a4: 'cur', a5: 'cur' } });
    m.page = 0;
    rec.step(4, '翻回第 1 屏：real_ 已经有了，代理直接转发，不再解码。解码次数还是 6。', { calls: [['p0', 'r0', 'draw', 'ok'], ['p1', 'r1', 'draw', 'ok'], ['p2', 'r2', 'draw', 'ok']] });
    rec.step(4, '虚代理把解码从「打开时」挪到了「第一次显示时」。全都翻一遍之后，内存和全部解码一样多——要让内存有上限，代理还得会卸载（练习 2）。', { hl: { r0: 'warn', r1: 'warn', r2: 'warn', r3: 'warn', r4: 'warn', r5: 'warn' }, note: '省的是启动时间，不是总工作量' });
  }

  function cache(m, p, rec) {
    reset(m, 'cache');
    rec.step(6, '缓存代理：CachingProbe 和 FileProbe 实现同一个接口，最多记住 2 条结果。');
    function ask(key) {
      m.req = key;
      var i = m.cache.indexOf(key);
      if (i >= 0) {
        m.hits++;
        m.cache.splice(i, 1); m.cache.unshift(key);
        var h = { cp: 'ok' }; h['c_' + key] = 'ok';
        rec.step(6, 'probe("' + key + '")：缓存里有，直接返回，不打开文件；它被挪到最前面（刚用过）。', { hl: h, calls: [['cli', 'cp', key], ['cp', 'cli', '命中', 'ok']] });
      } else {
        m.misses++;
        var evicted = m.cache.length === 2 ? m.cache[1] : null;
        var hb = { fp: 'cur' };
        if (evicted) hb['c_' + evicted] = 'warn';
        rec.step(6, 'probe("' + key + '")：缓存里没有，转发给 FileProbe 真正打开文件。' + (evicted ? '缓存满了，要淘汰最久没用的 ' + evicted + '。' : ''),
          { hl: hb, calls: [['cli', 'cp', key], ['cp', 'fp', key, 'warn']] });
        if (evicted) m.cache.pop();
        m.cache.unshift(key);
        var hn = {}; hn['c_' + key] = 'new';
        rec.step(6, '结果记进缓存，再返回给调用方。', { hl: hn, calls: [['fp', 'cp', 'info'], ['cp', 'cli', 'info']] });
      }
    }
    ['a', 'b', 'a', 'c', 'b'].forEach(ask);
    rec.step(6, '5 次请求命中 1 次。容量越大、请求越集中在少数热门文件上，命中率越高；本文的实测里，容量 20 能命中 57%。', { note: '缓存代理：用内存换时间，要想好容量和什么时候失效' });
  }

  DSViz.register('dp-proxy', {
    title: '虚代理和缓存代理：什么时候真正干活',
    width: 760,
    height: 296,
    code: [
      'Gallery g(6);              // 每格放一个 ThumbProxy',
      'g.show_page(0);            // proxy.draw()：第一次才 new VideoThumb',
      'g.show_page(1);',
      'g.show_page(0);            // 已有真实对象，直接转发',
      'Gallery g(6);              // 不用代理：每格直接 make_unique<VideoThumb>',
      'cache.probe(path);         // 缓存代理：查过就记住（LRU，容量 2）'
    ],
    init: function () { var m = {}; reset(m, 'proxy'); return m; },
    view: view,
    start: function (m, rec) { lazy(m, {}, rec); },
    actions: [{ label: '全部解码', run: eager }, { label: '虚代理', run: lazy }, { label: '缓存代理', run: cache }]
  });
})();
