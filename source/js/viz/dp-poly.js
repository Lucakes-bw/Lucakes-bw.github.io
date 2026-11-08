/* 设计模式专栏《组合优于继承》：为什么「类型随机」比「按类型排序」慢好几倍——一个简化的分支预测模型（dp-poly） */
(function () {
  'use strict';
  var RANDOM = 'CRRTCCTRTTCR'.split('');
  var SORTED = 'CCCCRRRRTTTT'.split('');
  var NAME = { C: '圆', R: '矩形', T: '三角' };
  var X0 = 24, W = 52, G = 6, Y = 80;

  function view(m, x) {
    x = x || {};
    var boxes = [], texts = [];
    m.seq.forEach(function (k, i) {
      var st = x.st && x.st[i];
      boxes.push({ id: 'e' + i, x: X0 + i * (W + G), y: Y, w: W, h: 40, text: NAME[k], sub: st === 'hit' ? '✓' : st === 'miss' ? '✗' : '',
        hl: x.at === i ? (st === 'miss' ? 'warn' : 'cur') : st === 'miss' ? 'warn' : st === 'hit' ? 'ok' : null });
    });
    texts.push({ id: 'title', x: X0, y: 24, text: m.title });
    if (x.guess) texts.push({ id: 'guess', x: X0, y: 52, text: x.guess, cls: 'muted' });
    texts.push({ id: 'cnt', x: X0, y: 170, text: '猜错 ' + (x.miss || 0) + ' 次 / 已处理 ' + (x.done || 0) + ' 个' });
    if (x.note) texts.push({ id: 'note', x: X0, y: 194, text: x.note, cls: 'muted' });
    return { kind: 'free', boxes: boxes, texts: texts };
  }

  function play(seq, title, m, rec) {
    m.seq = seq; m.title = title;
    var st = {}, prev = null, miss = 0;
    rec.step(1, title + '。CPU 不会等算出「这个元素是什么类型」再决定跳到哪段代码，而是先猜一个，边猜边往下执行；猜错了就把做了一半的工作全部丢掉，重新来过。', { done: 0, miss: 0 });
    seq.forEach(function (k, i) {
      var guess = prev == null ? null : prev, hit = guess === k;
      if (!hit) miss++;
      st[i] = hit ? 'hit' : 'miss';
      rec.step(3, '第 ' + (i + 1) + ' 个是' + NAME[k] + '。' + (guess == null ? '第一个元素，没有历史可参考，算猜错。' :
        hit ? '猜的是' + NAME[guess] + '，猜对了，流水线不停。' : '猜的是' + NAME[guess] + '，猜错了：丢掉已经开始执行的指令，要白白损失十几个时钟周期。'),
        { at: i, st: Object.assign({}, st), miss: miss, done: i + 1, guess: guess == null ? '预测：（没有历史）' : '预测：和上一个一样，是' + NAME[guess] });
      prev = k;
    });
    rec.step(3, '处理完 ' + seq.length + ' 个元素，猜错 ' + miss + ' 次。' + (miss > 4 ? '类型随机时，大部分元素都会猜错：三种类型随机出现，「和上一个一样」只有三分之一的机会猜对。' : '按类型排好序以后，只有类型切换的地方猜错。'),
      { st: st, miss: miss, done: seq.length, note: '真实 CPU 的预测器比「和上一个一样」聪明得多，但对真正随机的类型序列同样猜不准' });
  }

  DSViz.register('dp-poly', {
    title: '类型随机 vs 按类型排序：分支预测猜错了几次',
    width: 740,
    height: 210,
    code: [
      'double s = 0;',
      'for (const auto& v : shapes)',
      '    s += std::visit([](const auto& x) { return x.area(); }, v);   // 按类型跳到不同的代码'
    ],
    init: function () { return { seq: RANDOM, title: '类型随机的 12 个形状' }; },
    view: view,
    start: function (m, rec) { play(RANDOM, '类型随机的 12 个形状', m, rec); },
    actions: [
      { label: '类型随机', run: function (m, p, rec) { play(RANDOM, '类型随机的 12 个形状', m, rec); } },
      { label: '按类型排序', run: function (m, p, rec) { play(SORTED, '按类型排好序的 12 个形状', m, rec); } }
    ]
  });
})();
