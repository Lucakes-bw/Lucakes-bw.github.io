/* 设计模式专栏《观察者》：通知途中有观察者退订 / 订阅——range-for 迭代器失效，以及延迟删除怎么避开它（dp-observer） */
(function () {
  'use strict';

  var CW = 112, X0 = 20;
  var NAMES = ['首帧统计', '进度条', '字幕', '歌词'];

  function view(m, x) {
    x = x || {};
    var hl = x.hl || {}, boxes = [], texts = [];
    texts.push({ id: 'ta', x: X0, y: 24, text: m.newBuf ? 'observers_ 的旧内存（已经释放）' : 'observers_：size = ' + m.size + '，capacity = ' + m.cap, cls: 'muted' });
    m.slots.forEach(function (s, i) {
      var stale = i >= m.size && s;
      var h = hl['s' + i] || (m.freed ? 'warn' : stale ? 'warn' : null);
      boxes.push({ id: 's' + i, x: X0 + i * CW, y: 36, w: CW - 12, h: 40,
        text: s === null ? '（空）' : s || '—', sub: '[' + i + ']' + (stale ? ' size 之外' : ''), hl: h });
    });
    if (m.newBuf) {
      texts.push({ id: 'tb', x: X0, y: 150, text: 'push_back 扩容后的新内存：size = ' + m.newBuf.length + '，capacity = 8', cls: 'muted' });
      m.newBuf.forEach(function (s, i) {
        boxes.push({ id: 'n' + i, x: X0 + i * CW, y: 162, w: CW - 12, h: 40, text: s, sub: '[' + i + ']', hl: hl['n' + i] || (i === 4 ? 'new' : null) });
      });
    }
    if (m.it != null) texts.push({ id: 'it', x: X0 + m.it * CW + 8, y: 120, text: '↑ ' + (m.idx ? 'i = ' + m.it : 'it'), cls: m.itBad ? 'warn' : '' });
    if (m.end != null) texts.push({ id: 'end', x: X0 + m.end * CW + 8, y: 120, text: '↑ ' + (m.idx ? 'n = ' + m.end : '缓存的 end'), cls: 'muted' });
    texts.push({ id: 'tr', x: X0, y: 246, text: '这一轮谁收到了通知', cls: 'muted' });
    m.names.concat(m.extra ? ['卡顿检测'] : []).forEach(function (n, i) {
      var c = m.recv[n] || 0;
      boxes.push({ id: 'r' + i, x: X0 + i * CW, y: 258, w: CW - 12, h: 40, text: n, sub: '收到 ' + c + ' 次', hl: hl['r' + i] });
    });
    if (x.note) texts.push({ id: 'note', x: X0, y: 344, text: x.note, cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: [], texts: texts };
  }

  function reset(m) {
    m.names = NAMES.slice(); m.slots = NAMES.slice(); m.size = 4; m.cap = 4; m.recv = {}; m.it = null; m.end = null;
    m.idx = false; m.itBad = false; m.newBuf = null; m.freed = false; m.extra = false;
  }
  function call(m, i, rec, line, text, more) {
    var n = m.slots[i];
    m.recv[n] = (m.recv[n] || 0) + 1;
    var h = {};
    h['s' + i] = 'cur';
    h['r' + m.names.indexOf(n)] = m.recv[n] > 1 ? 'warn' : 'ok';
    rec.step(line, text, Object.assign({ hl: h }, more || {}));
  }

  // 1. range-for 里 erase
  function eraseBug(m, p, rec) {
    reset(m);
    m.it = 0; m.end = 4;
    rec.step(2, 'range-for 开始：先取 begin 和 end 存起来。end 指向第 4 格之后，整个循环里不会再重新取。', { hl: { s0: 'cur' } });
    call(m, 0, rec, 3, '通知第 0 个：首帧统计。它上报完首帧耗时，在回调里调用 player.detach(this)。');
    m.slots = ['进度条', '字幕', '歌词', '歌词']; m.size = 3;
    rec.step(9, 'erase 第 0 个：后面三个整体前移一格，size 变成 3。第 3 格的内存还在，里面残留着「歌词」的旧值。it 没动，现在指着「进度条」。', { hl: { s0: 'cmp', s1: 'cmp', s2: 'cmp' }, note: 'erase 之后，it 和 end 都已经失效（标准说用它们是未定义行为）' });
    m.it = 1;
    rec.step(2, '回到 range-for：++it，指向第 1 格「字幕」。刚挪到第 0 格的「进度条」被跳过了。', { hl: { s0: 'warn', s1: 'cur', r1: 'warn' } });
    call(m, 1, rec, 3, '通知字幕。');
    m.it = 2;
    call(m, 2, rec, 3, '++it，通知歌词。');
    m.it = 3; m.itBad = true;
    call(m, 3, rec, 3, '++it 到第 3 格。缓存的 end 还在第 4 格之后，于是循环继续，读到 size 之外残留的「歌词」，歌词收到第二次。', { note: 'Clang 的 ASan 在这里报 container-overflow；GCC 要加 -D_GLIBCXX_SANITIZE_VECTOR 才报' });
    m.it = 4;
    rec.step(4, '走到缓存的 end，循环结束。这一轮：进度条 0 次，歌词 2 次。', { hl: { r1: 'warn', r3: 'warn' }, note: '漏了一个、重复一个，而且只在有人「在回调里退订」时才发生' });
  }

  // 2. 下标遍历 + 延迟删除
  function deferred(m, p, rec) {
    reset(m);
    m.idx = true; m.it = 0; m.end = 4;
    rec.step(6, '延迟删除版：depth_ 加一，记下这一轮只通知开始时就有的 n = 4 个，用下标遍历。', { hl: { s0: 'cur' } });
    call(m, 0, rec, 7, '通知第 0 个：首帧统计，它在回调里 detach 自己。');
    m.slots = [null, '进度条', '字幕', '歌词'];
    rec.step(9, 'detach 发现 depth_ > 0（正在通知），不 erase，只把这一格置成空指针。vector 的结构完全不变。', { hl: { s0: 'cmp' } });
    m.it = 1;
    call(m, 1, rec, 7, 'i = 1：进度条照常收到。');
    m.it = 2;
    call(m, 2, rec, 7, 'i = 2：字幕。');
    m.it = 3;
    call(m, 3, rec, 7, 'i = 3：歌词。');
    m.it = null; m.end = null; m.slots = ['进度条', '字幕', '歌词', ''];
    m.size = 3;
    rec.step(8, '循环结束，depth_ 回到 0，这时才把空指针统一删掉。每个观察者恰好收到一次。', { hl: { r0: 'ok', r1: 'ok', r2: 'ok', r3: 'ok' }, note: '要删的先做标记，没人在遍历时再真正删除：这就是延迟删除' });
  }

  // 3. range-for 里 push_back
  function attachBug(m, p, rec) {
    reset(m);
    m.slots = ['统计', '进度条', '字幕', '歌词']; m.names = m.slots.slice();
    m.it = 0; m.end = 4;
    rec.step(2, '换一种情况：名单里正好 4 个，capacity 也是 4，已经满了。range-for 记下 begin、end。', { hl: { s0: 'cur' } });
    m.recv['统计'] = 1;
    rec.step(3, '通知第 0 个：统计。它第一次收到进度，在回调里 attach 一个「卡顿检测」。', { hl: { s0: 'cur', r0: 'ok' } });
    m.newBuf = ['统计', '进度条', '字幕', '歌词', '卡顿检测']; m.freed = true; m.extra = true;
    rec.step(3, 'push_back 第 5 个：容量不够，vector 分配一块新内存，把 5 个指针放进去，然后释放旧内存。', { hl: { n4: 'new' }, note: '外面的 it 和 end 还指着旧内存' });
    m.it = 1; m.itBad = true;
    rec.step(2, '回到 range-for：++it，*it 读的是已经释放的旧内存。ASan 报 heap-use-after-free；不加 ASan 时 -O2 下直接段错误。', { hl: { s1: 'warn' }, note: '名单没满时 push_back 不扩容，一切正常——所以这个 bug 只在「正好满」时出现' });
    m.it = null; m.end = null; m.itBad = false;
    rec.step(7, '下标遍历就没有这个问题：observers_[1] 每次都从 vector 当前的数据指针算，扩容后读的是新内存；n 是开始时的 4，新来的卡顿检测下一轮才收到。', { hl: { n1: 'ok' } });
  }

  DSViz.register('dp-observer', {
    title: '通知途中增删观察者：迭代器失效与延迟删除',
    width: 580,
    height: 356,
    code: [
      'void Player::tick(double pos) {',
      '  for (auto* o : observers_)          // 先记下 begin、end',
      '    o->onProgress(pos);               // 回调里可能 detach / attach',
      '}',
      '// ---- 延迟删除版 ----',
      '  ++depth_; const size_t n = observers_.size();',
      '  for (size_t i = 0; i < n; ++i) if (auto* o = observers_[i]) o->onProgress(pos);',
      '  if (--depth_ == 0) /* erase 所有 nullptr */;',
      'void detach(o) { if (depth_ > 0) *it = nullptr; else observers_.erase(it); }'
    ],
    init: function () { var m = {}; reset(m); return m; },
    view: view,
    start: function (m, rec) { eraseBug(m, {}, rec); },
    actions: [
      { label: '边遍历边 erase', run: eraseBug },
      { label: '延迟删除', run: deferred },
      { label: '边遍历边 push_back', run: attachBug }
    ]
  });
})();
