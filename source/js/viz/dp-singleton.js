/* 设计模式专栏《单例》：两个线程同时第一次调用 instance()——朴素懒汉式 vs Meyers 单例（dp-singleton） */
(function () {
  'use strict';
  var LANE = { t1: 40, t2: 130 };

  function view(m, x) {
    x = x || {};
    var hl = x.hl || {}, boxes = [], arrows = [], texts = [];
    ['t1', 't2'].forEach(function (t, i) {
      texts.push({ id: 'n' + t, x: 10, y: LANE[t] + 24, text: '线程 ' + (i + 1) });
      boxes.push({ id: t, x: 80, y: LANE[t], w: 270, h: 40, text: m[t], hl: hl[t] });
    });
    boxes.push({ id: 'var', x: 400, y: 85, w: 140, h: 40, text: m.var, sub: m.varLabel, hl: hl['var'] });
    m.objs.forEach(function (o, i) {
      boxes.push({ id: 'o' + i, x: 600, y: 40 + i * 90, w: 120, h: 40, text: o, sub: '堆上的 Config', hl: hl['o' + i] });
    });
    (x.arrows || []).forEach(function (a) { arrows.push({ from: a[0], to: a[1], hl: a[2] || null, label: a[3] }); });
    if (x.note) texts.push({ id: 'note', x: 10, y: 205, text: x.note, cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts };
  }

  function naive(m, p, rec) {
    m.t1 = '还没调用'; m.t2 = '还没调用'; m.var = 'nullptr'; m.varLabel = 'inst_'; m.objs = [];
    rec.step(1, '朴素懒汉式。两个线程几乎同时第一次调用 instance()，此时 inst_ 还是 nullptr。');
    m.t1 = 'if (inst_ == nullptr)  → 是空的';
    rec.step(2, '线程 1 检查 inst_：是空的，准备创建。', { hl: { t1: 'cur', 'var': 'cmp' } });
    m.t2 = 'if (inst_ == nullptr)  → 也是空的';
    rec.step(2, '线程 1 还没来得及赋值，线程 2 也检查了 inst_：它看到的同样是空的。两个线程都会去创建对象。', { hl: { t2: 'warn', 'var': 'cmp' } });
    m.t1 = 'inst_ = new Config;'; m.objs.push('Config #1'); m.var = '→ #1';
    rec.step(3, '线程 1 创建了 Config #1（构造函数要读配置文件，花了 10 毫秒），把地址存进 inst_。', { hl: { t1: 'cur', o0: 'new' }, arrows: [['var', 'o0', 'cur']] });
    m.t2 = 'inst_ = new Config;'; m.objs.push('Config #2'); m.var = '→ #2';
    rec.step(3, '线程 2 也创建了一个 Config #2，覆盖了 inst_。Config #1 从此再也没人能找到：内存泄漏。', { hl: { t2: 'warn', o1: 'new', o0: 'warn' }, arrows: [['var', 'o1', 'cur']] });
    m.t1 = 'return inst_;  → 拿到 #1'; m.t2 = 'return inst_;  → 拿到 #2';
    rec.step(4, '两个线程拿到了两个不同的「单例」。实测 8 个线程同时访问，5 次运行全都创建了 8 个对象；ThreadSanitizer 在赋值那一行报告了数据竞争。',
      { hl: { t1: 'warn', t2: 'warn' }, arrows: [['t1', 'o0', 'warn'], ['t2', 'o1', 'warn']], note: '对 inst_ 的读写没有任何同步，这本身就是未定义行为' });
  }

  function meyers(m, p, rec) {
    m.t1 = '还没调用'; m.t2 = '还没调用'; m.var = '未初始化'; m.varLabel = '守卫标志'; m.objs = [];
    rec.step(5, 'Meyers 单例。编译器为局部静态变量 inst 生成了一个隐藏的「守卫标志」，记录它是否已经初始化好。');
    m.t1 = '检查守卫：未初始化 → 加锁';
    rec.step(6, '线程 1 检查守卫标志：还没初始化，于是加锁，开始构造。', { hl: { t1: 'cur', 'var': 'cmp' } });
    m.t2 = '检查守卫：正在初始化 → 等待';
    rec.step(6, '线程 2 也来了：发现另一个线程正在初始化，于是等待，而不是自己再构造一个。', { hl: { t2: 'cmp', 'var': 'cmp' } });
    m.objs.push('Config'); m.var = '已初始化'; m.t1 = '构造完成，设置守卫，解锁';
    rec.step(6, '线程 1 构造完成，把守卫标志设为「已初始化」，唤醒等待的线程。', { hl: { t1: 'cur', o0: 'new', 'var': 'ok' } });
    m.t1 = 'return inst;'; m.t2 = 'return inst;';
    rec.step(7, '两个线程拿到同一个对象，构造函数只执行了一次。以后每次调用 instance()，只需读一下守卫标志、发现已经初始化，直接返回。',
      { hl: { t1: 'ok', t2: 'ok', 'var': 'ok' }, arrows: [['t1', 'o0', 'ok'], ['t2', 'o0', 'ok']], note: '这是 C++11 标准的保证：局部静态变量的初始化是线程安全的' });
  }

  DSViz.register('dp-singleton', {
    title: '两个线程同时第一次调用 instance()',
    width: 740,
    height: 220,
    code: [
      '// 朴素懒汉式',
      'if (inst_ == nullptr)',
      '    inst_ = new Config;',
      'return inst_;',
      '// Meyers 单例',
      'static Config inst;     // 编译器生成：检查守卫 → 加锁 → 构造 → 设置守卫 → 解锁',
      'return inst;'
    ],
    init: function () { return { t1: '', t2: '', 'var': 'nullptr', varLabel: 'inst_', objs: [] }; },
    view: view,
    start: function (m, rec) { naive(m, {}, rec); },
    actions: [{ label: '朴素懒汉式', run: naive }, { label: 'Meyers 单例', run: meyers }]
  });
})();
