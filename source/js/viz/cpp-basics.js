/* C++ 专栏：vector 扩容（vec-grow，《STL 容器》）和数据竞争（race，《线程与互斥锁》） */
(function () {
  'use strict';

  /* ---------------- vector 扩容 ----------------
     每一代内存块的格子用 g{代}_{下标} 作为 id：新块先画在下面一行，旧块释放后，新块移到上面一行，有移动的动画 */
  var CW = 40, X0 = 110;

  function vecView(m, x) {
    var boxes = [], texts = [], arrows = [];
    function block(b, y, hl, label) {
      texts.push({ id: 'L' + b.gen, x: X0 - 12, y: y + 24, text: label, anchor: 'end', cls: 'muted' });
      for (var i = 0; i < b.cap; i++) {
        var has = i < b.vals.length;
        boxes.push({ id: 'g' + b.gen + '_' + i, x: X0 + i * CW, y: y, w: CW, h: 36, text: has ? b.vals[i] : '',
          hl: !has ? 'empty' : (x.hlIdx === i && b === m.cur ? 'new' : hl) });
      }
    }
    if (m.cur) block(m.cur, 60, null, m.next ? '旧内存' : '当前内存');
    if (m.next) block(m.next, 150, 'cur', '新内存');
    if (m.cur && m.next && m.next.vals.length) texts.push({ id: 'mv', x: X0, y: 124, text: '↓ 逐个搬到新内存' });
    if (!m.cur && !m.next) texts.push({ id: 'e', x: X0, y: 84, text: '空的 vector：size = 0，capacity = 0，还没有分配任何内存', cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts,
      vars: { size: m.size, capacity: m.cur ? m.cur.cap : 0, '累计搬移': m.moved } };
  }

  function vecRun(m, p, rec) {
    m.cur = null; m.next = null; m.size = 0; m.moved = 0; m.gen = 0;
    rec.step(1, '一开始 vector 是空的。点「播放」看它怎样一边添加元素一边扩容。');
    for (var v = 0; v < p.n; v++) {
      var cap = m.cur ? m.cur.cap : 0;
      if (m.size === cap) {
        var nc = cap === 0 ? 1 : cap * 2;
        m.next = { gen: ++m.gen, cap: nc, vals: [] };
        rec.step(5, 'push_back(' + v + ')：size = capacity = ' + cap + '，放不下了。分配一块能放 ' + nc + ' 个元素的新内存' + (cap ? '（原来的 2 倍）' : '') + '。');
        if (m.cur && m.cur.vals.length) {
          m.next.vals = m.cur.vals.slice();
          m.moved += m.cur.vals.length;
          rec.step(6, '把旧内存中的 ' + m.cur.vals.length + ' 个元素搬到新内存（对 int 是复制，对类类型是移动或复制构造）。');
        }
        if (m.cur) {
          m.cur = m.next; m.next = null;
          rec.step(7, '释放旧内存。之前指向旧内存中元素的指针、引用、迭代器从此全部失效。');
        } else { m.cur = m.next; m.next = null; }
      }
      m.cur.vals.push(v);
      m.size++;
      rec.step(9, 'push_back(' + v + ')：放进第 ' + (m.size - 1) + ' 个位置，size = ' + m.size + '。', { hlIdx: m.size - 1 });
    }
    rec.step(0, '添加了 ' + p.n + ' 个元素，一共搬移了 ' + m.moved + ' 次。容量每次翻倍，搬移的总次数不会超过 2n（这里是 ' + (2 * p.n) +
      '），平均到每次 push_back 只是常数次，这就是「均摊 O(1)」。');
  }

  DSViz.register('vec-grow', {
    title: 'vector 的 push_back 与扩容',
    height: 210,
    code: [
      'std::vector<int> v;',
      'for (int i = 0; i < n; ++i) v.push_back(i);',
      '',
      '// push_back 内部（简化）：',
      'if (size == capacity) {      // 满了：分配 2 倍的新内存',
      '    把旧元素逐个搬到新内存;',
      '    释放旧内存;',
      '}',
      'data[size++] = value;'
    ],
    init: function () { return { cur: null, next: null, size: 0, moved: 0, gen: 0 }; },
    view: vecView,
    start: function (m, rec) { vecRun(m, { n: 9 }, rec); },
    actions: [{
      label: 'push_back n 个',
      fields: [{ key: 'n', label: 'n', value: 9, size: 3 }],
      check: function (m, p) { return p.n >= 1 && p.n <= 15 ? null : 'n 取 1 到 15（屏幕放不下更多的格子）'; },
      run: vecRun
    }]
  });

  /* ---------------- 数据竞争：两个线程同时 ++counter ---------------- */
  function raceView(m, x) {
    var hl = x.hl || {};
    var boxes = [
      { id: 'mem', x: 280, y: 40, w: 160, h: 40, text: 'counter = ' + m.mem, hl: hl.mem },
      { id: 'ra', x: 40, y: 140, w: 170, h: 36, text: '寄存器 r = ' + m.A.r, hl: hl.A },
      { id: 'rb', x: 510, y: 140, w: 170, h: 36, text: '寄存器 r = ' + m.B.r, hl: hl.B }
    ];
    if (m.locked !== undefined) boxes.push({ id: 'lk', x: 300, y: 140, w: 120, h: 36, text: m.locked ? '锁：' + m.locked + ' 持有' : '锁：空闲', hl: m.locked ? 'warn' : null });
    var texts = [
      { id: 'tm', x: 360, y: 28, text: '内存', anchor: 'middle', cls: 'muted' },
      { id: 'ta', x: 125, y: 128, text: '线程 A', anchor: 'middle' },
      { id: 'tb', x: 595, y: 128, text: '线程 B', anchor: 'middle' },
      { id: 'sa', x: 125, y: 198, text: m.A.s || '', anchor: 'middle', cls: 'muted' },
      { id: 'sb', x: 595, y: 198, text: m.B.s || '', anchor: 'middle', cls: 'muted' }
    ];
    var arrows = (x.arrows || []).map(function (a) { return { from: a[0], to: a[1], hl: 'new', label: a[2] }; });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts, vars: x.vars };
  }

  function raceUnsafe(m, p, rec) {
    m.mem = 5; m.A = { r: '?', s: '' }; m.B = { r: '?', s: '' }; delete m.locked;
    rec.step(0, '不加锁。counter 现在是 5，两个线程都要执行一次 ++counter，结果应该是 7。');
    m.A.r = 5; m.A.s = '读到 5';
    rec.step(2, '线程 A：读取 counter，寄存器中是 5。', { hl: { A: 'cur' }, arrows: [['mem', 'ra', '读']] });
    m.B.r = 5; m.B.s = '也读到 5';
    rec.step(2, '线程 A 还没来得及写回，线程 B 也读取了 counter，读到的同样是 5。', { hl: { B: 'cur' }, arrows: [['mem', 'rb', '读']] });
    m.A.r = 6; m.A.s = '算出 6';
    rec.step(3, '线程 A：寄存器加 1，得到 6。', { hl: { A: 'cur' } });
    m.mem = 6; m.A.s = '写回 6';
    rec.step(4, '线程 A：把 6 写回 counter。', { hl: { mem: 'cur', A: 'cur' }, arrows: [['ra', 'mem', '写']] });
    m.B.r = 6; m.B.s = '算出 6';
    rec.step(3, '线程 B：用自己寄存器中的旧值 5 加 1，也得到 6。', { hl: { B: 'cur' } });
    m.mem = 6; m.B.s = '写回 6';
    rec.step(4, '线程 B：把 6 写回 counter，覆盖了 A 的结果。', { hl: { mem: 'warn', B: 'cur' }, arrows: [['rb', 'mem', '写']] });
    rec.step(0, '两次 ++counter，counter 只从 5 变成了 6，A 的那次加法丢失了。每次丢失多少取决于两个线程恰好怎样交错，所以每次运行的结果都不一样。', { hl: { mem: 'warn' } });
  }

  function raceLocked(m, p, rec) {
    m.mem = 5; m.A = { r: '?', s: '' }; m.B = { r: '?', s: '' }; m.locked = '';
    rec.step(0, '加锁。counter 现在是 5，两个线程都先 lock，再执行 ++counter。');
    m.locked = 'A'; m.A.s = '拿到锁';
    rec.step(1, '线程 A 先拿到了锁。', { hl: { A: 'cur' } });
    m.B.s = '等待锁…';
    rec.step(1, '线程 B 也想加锁，锁被 A 持有，B 只能停下来等待。', { hl: { B: 'warn' } });
    m.A.r = 5; m.A.s = '读到 5';
    rec.step(2, '线程 A：读取 counter，寄存器中是 5。B 还在等，没有机会读。', { hl: { A: 'cur', B: 'dim' }, arrows: [['mem', 'ra', '读']] });
    m.A.r = 6; m.A.s = '算出 6';
    rec.step(3, '线程 A：加 1，得到 6。', { hl: { A: 'cur', B: 'dim' } });
    m.mem = 6; m.A.s = '写回 6';
    rec.step(4, '线程 A：写回 6。', { hl: { mem: 'cur', A: 'cur', B: 'dim' }, arrows: [['ra', 'mem', '写']] });
    m.locked = 'B'; m.A.s = '解锁'; m.B.s = '拿到锁';
    rec.step(5, '线程 A 解锁。B 终于拿到了锁。', { hl: { B: 'cur' } });
    m.B.r = 6; m.B.s = '读到 6';
    rec.step(2, '线程 B：读取 counter，这次读到的是 A 写回的 6。', { hl: { B: 'cur' }, arrows: [['mem', 'rb', '读']] });
    m.B.r = 7; m.B.s = '算出 7';
    rec.step(3, '线程 B：加 1，得到 7。', { hl: { B: 'cur' } });
    m.mem = 7; m.B.s = '写回 7';
    rec.step(4, '线程 B：写回 7。', { hl: { mem: 'ok', B: 'cur' }, arrows: [['rb', 'mem', '写']] });
    m.locked = ''; m.B.s = '解锁';
    rec.step(5, '结果是 7，一次都没有丢。锁让「读、加、写」三步成为一个整体，同一时刻只有一个线程在执行它们，代价是 B 要等待。', { hl: { mem: 'ok' } });
  }

  DSViz.register('race', {
    title: '两个线程同时执行 ++counter',
    height: 215,
    code: [
      'mtx.lock();          // 只有「加锁」时才有这一行',
      'int r = counter;     // ① 读到寄存器',
      'r = r + 1;           // ② 加 1',
      'counter = r;         // ③ 写回内存',
      'mtx.unlock();'
    ],
    init: function () { return { mem: 5, A: { r: '?' }, B: { r: '?' } }; },
    view: raceView,
    start: function (m, rec) { raceUnsafe(m, {}, rec); },
    actions: [
      { label: '不加锁', run: raceUnsafe },
      { label: '加锁', run: raceLocked }
    ]
  });

  /* ---------------- remove_if：把要保留的元素往前搬（《迭代器与算法》） ---------------- */
  function rmView(m, x) {
    var cells = m.v.map(function (v, i) {
      var hl = null;
      if (m.done && i >= m.w) hl = 'dim';
      if (x.hl && x.hl[i]) hl = x.hl[i];
      return { v: v, hl: hl };
    });
    var ptrs = [];
    if (m.r != null && m.r < m.v.length) ptrs.push({ name: 'i', at: m.r });
    if (m.w != null) ptrs.push({ name: m.done ? 'newEnd' : 'first', at: m.w, hl: m.done ? 'warn' : null });
    return { kind: 'array', cells: cells, cap: m.cap, ptrs: ptrs, ptrsBelow: true, top: 40, x0: 120, cellW: 60,
      vars: { size: m.v.length } };
  }

  function rmRun(m, p, rec) {
    m.v = [1, 2, 2, 3, 4, 4, 5]; m.cap = 7; m.r = null; m.w = null; m.done = false;
    rec.step(1, 'v = {1, 2, 2, 3, 4, 4, 5}，要删掉所有的偶数。');
    m.w = 0;
    while (m.w < m.v.length && m.v[m.w] % 2) {
      rec.step(3, 'first 从头开始找第一个要删除的元素：' + m.v[m.w] + ' 是奇数，保留，继续往后找。', { hl: (function (o) { o[m.w] = 'cmp'; return o; })({}) });
      m.w++;
    }
    rec.step(3, m.v[m.w] + ' 是偶数，找到了：first 停在下标 ' + m.w + '。从这里开始，first 表示「下一个保留的元素要放到哪里」。', { hl: (function (o) { o[m.w] = 'warn'; return o; })({}) });
    for (m.r = m.w + 1; m.r < m.v.length; m.r++) {
      var h = {}; h[m.r] = 'cmp';
      if (m.v[m.r] % 2 === 0) {
        rec.step(5, 'i 指向 ' + m.v[m.r] + '，是偶数，跳过。', { hl: h });
      } else {
        rec.step(5, 'i 指向 ' + m.v[m.r] + '，是奇数，要保留。', { hl: h });
        m.v[m.w] = m.v[m.r];
        var h2 = {}; h2[m.w] = 'new';
        rec.step(6, '把它移到 first 的位置（下标 ' + m.w + '），first 前进一格。', { hl: h2 });
        m.w++;
      }
    }
    m.r = null; m.done = true;
    rec.step(7, 'i 到了末尾。remove_if 返回 first，就是 newEnd（下标 ' + m.w + '）。前 ' + m.w + ' 个是保留的元素；后面的是剩下的旧值，内容没有意义。size 仍然是 7：算法只能通过迭代器读写元素，改变不了容器的大小。');
    m.v = m.v.slice(0, m.w);
    rec.step(9, 'v.erase(newEnd, v.end()) 才真正删掉尾部，size 变成 ' + m.w + '。整个过程只遍历了一次。');
  }

  DSViz.register('remove-if', {
    title: 'remove_if 做了什么：v.erase(remove_if(…), v.end())',
    height: 150,
    code: [
      'std::vector<int> v{1, 2, 2, 3, 4, 4, 5};',
      '// remove_if 内部（简化）：',
      'auto first = find_if(v.begin(), v.end(), 是偶数);',
      'for (auto i = first; ++i != v.end(); )',
      '    if (!是偶数(*i))',
      '        *first++ = std::move(*i);',
      'return first;                   // newEnd',
      '',
      'v.erase(newEnd, v.end());'
    ],
    init: function () { return { v: [], cap: 7 }; },
    view: rmView,
    start: function (m, rec) { rmRun(m, {}, rec); }
  });
})();
