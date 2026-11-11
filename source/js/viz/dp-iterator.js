/* 设计模式专栏《迭代器》：环形缓冲区的迭代器怎么走、写满覆盖后迭代器指向了什么、vector 扩容后的悬空迭代器（dp-iterator） */
(function () {
  'use strict';

  var X0 = 30, DX = 86, W = 74, Y = 56;

  function view(m, x) {
    x = x || {};
    var hl = x.hl || {}, boxes = [], texts = [], i;
    if (m.mode === 'vec') {
      texts.push({ id: 'h1', x: X0, y: 40, text: '旧的数组（容量 4）' + (m.freed ? '：已经 delete' : ''), cls: 'muted' });
      for (i = 0; i < 4; i++) {
        boxes.push({ id: 'o' + i, x: X0 + i * DX, y: Y, w: W, h: 40, text: m.freed ? '?' : 'dts ' + i * 40,
          sub: m.it === i && !m.moved ? 'it' : (m.it === i ? 'it（悬空）' : ''), hl: hl['o' + i] || (m.freed ? 'dim' : null) });
      }
      if (m.grown) {
        texts.push({ id: 'h2', x: X0, y: 150, text: '新的数组（容量 8）：元素搬过来，再放新包', cls: 'muted' });
        for (i = 0; i < 8; i++) {
          var t = i < 4 ? 'dts ' + i * 40 : (i === 4 ? '音频 0' : '');
          boxes.push({ id: 'n' + i, x: X0 + i * DX, y: 166, w: W, h: 40, text: t, hl: hl['n' + i] });
        }
      }
    } else {
      texts.push({ id: 'h1', x: X0, y: 40, text: '格子（数组下标）0～7，框里是帧号；head 是最老的帧', cls: 'muted' });
      for (i = 0; i < 8; i++) {
        var marks = [];
        if (i === m.head) marks.push('head');
        if (i === m.it) marks.push('it');
        boxes.push({ id: 's' + i, x: X0 + i * DX, y: Y, w: W, h: 40, text: m.slots[i] == null ? '' : '第 ' + m.slots[i] + ' 帧',
          sub: '[' + i + '] ' + marks.join(' '), hl: hl['s' + i] });
      }
      if (m.i != null) texts.push({ id: 'iv', x: X0, y: 140, text: m.i >= 8 ? '迭代器里存的是逻辑下标 i = 8，等于 end() 的 i，循环结束（end 不能解引用）' :
        '迭代器里存的是逻辑下标 i = ' + m.i + '，*it 取格子 (head + i) % 8 = (' + m.head + ' + ' + m.i + ') % 8 = ' + ((m.head + m.i) % 8), cls: hl.iv ? null : 'muted' });
      if (m.out != null) texts.push({ id: 'out', x: X0, y: 170, text: '输出：' + m.out });
    }
    if (x.note) texts.push({ id: 'note', x: X0, y: 240, text: x.note, cls: 'muted' });
    return { kind: 'free', boxes: boxes, texts: texts };
  }

  function ring(m, head, first) {   // 8 个格子，head 处是第 first 帧，依次往后
    m.mode = 'ring'; m.head = head; m.slots = [];
    for (var k = 0; k < 8; k++) m.slots[(head + k) % 8] = first + k;
    m.it = null; m.i = null; m.out = null;
  }

  function walk(m, p, rec) {
    ring(m, 3, 3);
    rec.step(1, '11 帧推进 8 个格子：第 0～2 帧被覆盖，最老的第 3 帧在格子 3（head = 3），最新的第 10 帧绕回到了格子 2。', { hl: { s3: 'cmp' } });
    m.i = 0; m.it = 3; m.out = '';
    rec.step(1, 'begin() 返回逻辑下标 i = 0 的迭代器。调用方只知道「第 0 个」，不知道它在格子 3。', { hl: { s3: 'cur', iv: 1 } });
    for (var i = 0; i < 8; i++) {
      m.i = i; m.it = (3 + i) % 8;
      m.out += (m.out ? ' ' : '') + m.slots[m.it];
      var h = {}; h['s' + m.it] = 'cur'; h.iv = 1;
      if (i === 5) rec.step(2, 'i = 5：(3 + 5) % 8 = 0，绕回到了数组开头。取模只写在迭代器的 operator* 里，调用方的循环里看不到。', { hl: h });
      else if (i === 0 || i === 7) rec.step(2, i === 0 ? '*it：取出第 3 帧，打印。' : 'i = 7：最后一帧（第 10 帧）。再 ++it，i = 8 等于 end() 的 i，循环结束。', { hl: h });
      else rec.step(2, '++it 只做 ++i，*it 再算格子：(3 + ' + i + ') % 8 = ' + m.it + '。', { hl: h });
    }
    m.it = null; m.i = 8;
    rec.step(1, '按时间顺序输出 3～10。暴露内部结构的版本里，dump() 直接从格子 0 打到格子 7，输出的是 8 9 10 3 4 5 6 7。', { hl: { out: 'ok' }, note: '迭代器把「第 i 个在哪个格子」这条知识留在了容器内部' });
  }

  function overwrite(m, p, rec) {
    ring(m, 0, 0);
    m.it = 0;
    rec.step(3, '缓冲区刚好写满：第 0～7 帧。渲染线程拿着 begin()，准备播第 0 帧。', { hl: { s0: 'cur' } });
    for (var k = 8; k < 11; k++) {
      var s = k - 8, h = {};
      m.slots[s] = k; m.head = (s + 1) % 8;
      h['s' + s] = 'new';
      rec.step(4, '解码线程又写入第 ' + k + ' 帧：缓冲区满了，覆盖格子 ' + s + ' 里最老的帧，head 移到 ' + m.head + '。', { hl: h });
    }
    rec.step(5, 'it 还指着格子 0，内存完好，ASan 也不会报错；但格子 0 里现在是第 8 帧。渲染线程以为在播第 0 帧，实际播的是第 8 帧，中间 7 帧被跳过。', { hl: { s0: 'warn' } });
    rec.step(5, '解决办法：迭代器里存只增不减的序号，解引用前检查序号是否还在 [read, write] 之间（overwrite.cpp 的 valid()）。', { hl: { s0: 'warn', s3: 'ok' }, note: '逻辑失效：指针没悬空，指向的东西换了' });
  }

  function grow(m, p, rec) {
    m.mode = 'vec'; m.it = 0; m.grown = false; m.freed = false; m.moved = false;
    rec.step(6, 'vector 里有 4 个视频包，容量也是 4。it 指向第一个包（本质上就是一个指针）。', { hl: { o0: 'cur' } });
    m.grown = true;
    rec.step(7, 'push_back 一个音频包：容量不够，分配一块 8 个元素的新内存，把 4 个包搬过去，再放新包。', { hl: { n0: 'new', n1: 'new', n2: 'new', n3: 'new', n4: 'new' } });
    m.freed = true; m.moved = true;
    rec.step(7, '旧内存被释放。it 里的指针没变，还指着旧内存。', { hl: { o0: 'warn' } });
    rec.step(8, '*it 读到的是已经释放的内存：-fsanitize=address 报 heap-use-after-free；不开 ASan 时 Clang 读出 4、GCC 读出一个很大的垃圾数，程序不崩。', { hl: { o0: 'warn', n0: 'ok' }, note: 'vector 扩容：所有迭代器、指针、引用一起失效' });
  }

  DSViz.register('dp-iterator', {
    title: '环形缓冲区的迭代器：怎么走、什么时候失效',
    width: 730,
    height: 256,
    code: [
      'for (auto it = ring.begin(); it != ring.end(); ++it)',
      '    print(*it);            // buf_[(head_ + i_) % 8]',
      'auto it = ring.begin();     // 记住最老的一帧',
      'ring.push(f);              // 写满后覆盖最老的帧',
      'use(*it);                  // it 还对吗？',
      'auto it = queue.begin();    // std::vector<Packet>',
      'queue.push_back(p);        // 容量 4 → 8，重新分配',
      'use(*it);                  // 悬空'
    ],
    init: function () { var m = {}; ring(m, 3, 3); return m; },
    view: view,
    start: function (m, rec) { walk(m, {}, rec); },
    actions: [{ label: '遍历', run: walk }, { label: '写满覆盖', run: overwrite }, { label: 'vector 扩容', run: grow }]
  });
})();
