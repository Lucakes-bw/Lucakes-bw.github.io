/* 客户端开发专栏《跨线程的信号与槽》：三种连接方式下，一次 emit 发生了什么（qt-conn） */
(function () {
  'use strict';
  var LANES = { ui: 50, wk: 140 };

  function view(m, x) {
    var hl = x.hl || {}, boxes = [], texts = [];
    [['ui', '界面线程', 's 在这里 emit'], ['wk', '工作线程', 'r 属于这里']].forEach(function (l) {
      var y = LANES[l[0]];
      texts.push({ id: 'n' + l[0], x: 20, y: y + 16, text: l[1] });
      texts.push({ id: 's' + l[0], x: 20, y: y + 34, text: l[2], cls: 'muted' });
      boxes.push({ id: 'run' + l[0], x: 120, y: y, w: 250, h: 40, text: m[l[0]], hl: hl[l[0]] });
      texts.push({ id: 'q' + l[0], x: 392, y: y + 25, text: '事件队列', cls: 'muted' });
      for (var i = 0; i < 3; i++) {
        var ev = m.q[l[0]][i];
        boxes.push({ id: 'c' + l[0] + i, x: 460 + i * 84, y: y + 2, w: 80, h: 36, text: ev || '', hl: ev ? (i === 0 && hl.q === l[0] ? 'new' : null) : 'empty' });
      }
    });
    var arrows = (x.arrows || []).map(function (a) { return { from: a[0], to: a[1], hl: a[2] || 'new', label: a[3] }; });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts, vars: x.vars };
  }

  function reset(m) { m.ui = 'main()：执行到 emit 这一行'; m.wk = '事件循环：等待事件'; m.q = { ui: [], wk: [] }; }

  function queued(m, p, rec) {
    reset(m);
    rec.step(1, 'AutoConnection。界面线程执行 emit s.ping(1)，接收者 r 属于工作线程。', { hl: { ui: 'cur' } });
    m.ui = 'emit s.ping(1)';
    m.q.wk.push('onPing(1)');
    rec.step(1, 'Qt 比较「当前线程」和「r 所属的线程」，两者不同，于是用队列方式：把这次调用连同参数的副本打包成一个事件，放进工作线程的事件队列。',
      { hl: { ui: 'cur', q: 'wk' }, arrows: [['runui', 'cwk0', 'new', '投递事件']] });
    m.ui = 'emit 返回，继续执行下一行';
    rec.step(2, 'emit 立即返回，界面线程继续往下执行。这时槽还没有执行。', { hl: { ui: 'cur', q: 'wk' } });
    m.q.wk.shift();
    m.wk = 'onPing(1)';
    rec.step(3, '工作线程的事件循环取出这个事件，在工作线程中调用 r.onPing(1)。槽在接收者所属的线程中执行，访问 r 不需要加锁。', { hl: { wk: 'cur' } });
    m.wk = '事件循环：等待事件';
    rec.step(3, '槽执行完，工作线程回到事件循环，等待下一个事件。输出的顺序是「emit 返回」在前、「onPing(1)」在后。');
  }

  function direct(m, p, rec) {
    reset(m);
    m.wk = '正在执行别的槽，也在访问 r';
    rec.step(1, 'DirectConnection。界面线程执行 emit s.ping(3)；与此同时，工作线程可能正在执行 r 的其他函数。', { hl: { ui: 'cur', wk: 'cur' } });
    m.ui = 'r.onPing(3)  ← 在界面线程中！';
    rec.step(1, '直接连接不看线程：emit 就是一次普通的函数调用，r.onPing(3) 在发出信号的界面线程中立即执行。', { hl: { ui: 'warn', wk: 'cur' } });
    rec.step(1, '现在两个线程同时在访问 r 的成员，这就是数据竞争（C++11 专栏《线程与互斥锁》）。用 DirectConnection 跨线程，就要自己保证槽是线程安全的。', { hl: { ui: 'warn', wk: 'warn' } });
    m.ui = 'emit 返回，继续执行下一行';
    rec.step(2, '槽执行完，emit 才返回。输出的顺序是「onPing(3)」在前、「emit 返回」在后。', { hl: { ui: 'cur' } });
  }

  function blocking(m, p, rec) {
    reset(m);
    rec.step(1, 'BlockingQueuedConnection。界面线程执行 emit s.ping(4)。', { hl: { ui: 'cur' } });
    m.q.wk.push('onPing(4)');
    m.ui = '阻塞：等待对方执行完';
    rec.step(1, '和队列连接一样，调用被打包成事件放进工作线程的队列；不同的是，界面线程停在 emit 这里等待。界面在这期间无法响应。',
      { hl: { ui: 'warn', q: 'wk' }, arrows: [['runui', 'cwk0', 'new', '投递事件并等待']] });
    m.q.wk.shift();
    m.wk = 'onPing(4)';
    rec.step(3, '工作线程取出事件，执行 r.onPing(4)。', { hl: { ui: 'warn', wk: 'cur' } });
    m.wk = '事件循环：等待事件';
    m.ui = 'emit 返回，继续执行下一行';
    rec.step(2, '槽执行完，Qt 唤醒等待中的界面线程，emit 返回。如果发送方和接收方在同一个线程，emit 等的是自己，永远等不到，这就是正文 2.5 节的死锁。',
      { hl: { ui: 'cur' } });
  }

  DSViz.register('qt-conn', {
    title: '一次 emit：队列连接、直接连接、阻塞队列连接',
    height: 200,
    code: [
      'emit s.ping(n);          // 界面线程中执行',
      'printf("emit 返回\\n");',
      '// 工作线程中：void Receiver::onPing(int n) { ... }'
    ],
    init: function () { var m = {}; reset(m); return m; },
    view: view,
    start: function (m, rec) { queued(m, {}, rec); },
    actions: [
      { label: '队列（Auto 跨线程）', run: queued },
      { label: '直接', run: direct },
      { label: '阻塞队列', run: blocking }
    ]
  });
})();
