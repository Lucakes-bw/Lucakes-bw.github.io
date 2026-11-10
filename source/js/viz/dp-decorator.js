/* 设计模式专栏《装饰器》：一次 read() 怎样一层层传进去、再一层层返回；换了包装顺序，统计结果就变了（dp-decorator） */
(function () {
  'use strict';

  var LABEL = { client: '使用者', Stats: 'Stats', Logging: 'Logging', Validate: 'Validate', Src: 'MemorySource' };
  var LINE = { client: 5, Stats: 6, Logging: 7, Validate: 8, Src: 9 };

  function sub(m, id) {
    if (id === 'client') return 'src->read(p)';
    if (id === 'Stats') return 'packets = ' + m.packets;
    if (id === 'Logging') return '日志 ' + m.logs + ' 行';
    if (id === 'Validate') return 'dropped = ' + m.dropped;
    return '下一个：#' + m.i;
  }

  function view(m, x) {
    x = x || {};
    var hl = x.hl || {}, boxes = [], arrows = [], texts = [];
    var ids = ['client'].concat(m.order);
    texts.push({ id: 'tt', x: 20, y: 22, text: '外层 → 里层：每个箭头表示外层持有一个 unique_ptr<PacketSource>，指向里层', cls: 'muted' });
    ids.forEach(function (id, k) {
      boxes.push({ id: id, x: 20 + k * 148, y: 40, w: 128, h: 50, text: LABEL[id], sub: sub(m, id), hl: hl[id] });
      if (k > 0) arrows.push({ from: ids[k - 1], to: id, hl: hl[id] === 'cur' && m.dir === 'in' ? 'cur' : undefined });
    });
    if (m.at) texts.push({ id: 'pk', x: 20 + ids.indexOf(m.at) * 148, y: 130, text: 'p = ' + m.pkt, cls: m.bad ? 'warn' : '' });
    if (x.note) texts.push({ id: 'note', x: 20, y: 160, text: x.note, cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts };
  }

  function reset(m, i, order) {
    m.order = order || ['Stats', 'Logging', 'Validate', 'Src'];
    m.i = i;
    m.packets = i;                               // 前面的 i 个包都是好包（第一个坏包是 #59）
    m.dropped = 0;
    m.logs = Math.ceil(i / 48);
    m.at = null; m.pkt = ''; m.bad = false; m.key = false; m.dir = 'in';
  }

  function hl(id, v) { var h = {}; h[id] = v; return { hl: h }; }

  // 从第 k 层开始往里转发，直到 MemorySource 生成一个包
  function down(m, rec, k, first) {
    var ids = ['client'].concat(m.order);
    m.dir = 'in';
    for (; k < ids.length; k++) {
      var id = ids[k];
      if (id === 'Src') break;
      rec.step(LINE[id], first && k === 1
        ? '使用者只认识 PacketSource，它调用的是最外层 ' + LABEL[id] + ' 的 read()。'
        : LABEL[id] + '::read() 先调用 inner_->read(p)，把请求交给里面一层。', hl(id, 'cur'));
    }
    var i = m.i++;
    m.bad = i % 60 === 59;
    m.key = i % 48 === 0;
    m.pkt = '#' + i + (m.bad ? ' 空包（坏包）' : m.key ? ' 关键帧 9000 字节' : ' ' + (1200 + i * 37 % 800) + ' 字节');
    m.at = 'Src'; m.dir = 'out';
    rec.step(LINE.Src, 'MemorySource 是被装饰的对象，只有它真正生成包：' + m.pkt + '。', hl('Src', m.bad ? 'warn' : 'new'));
  }

  // 包回到某一层，这一层做自己的事
  function at(m, rec, id) {
    m.at = id;
    if (id === 'Logging') {
      if (m.key) { m.logs++; rec.step(LINE[id], 'Logging 看到是关键帧，打一行日志，再返回。', hl(id, 'new')); }
      else rec.step(LINE[id], 'Logging 看到不是关键帧，不打日志，原样返回。', hl(id, 'cmp'));
    } else if (id === 'Stats') {
      m.packets++;
      if (m.bad) rec.step(LINE[id], 'Stats 在 Validate 里面，坏包还没被丢掉，它把空包也数了进去：packets + 1。', hl(id, 'warn'));
      else rec.step(LINE[id], 'Stats 数一下：packets + 1，字节数加上包的大小。', hl(id, 'new'));
    } else if (id === 'Validate') {
      if (m.bad) { m.dropped++; rec.step(LINE[id], 'Validate 发现是空包：丢掉它（dropped + 1），在 while 循环里再向里面要一个包。', hl(id, 'warn')); return false; }
      rec.step(LINE[id], 'Validate 检查包不是空的，放行。', hl(id, 'ok'));
    }
    return true;
  }

  function done(m, rec, note) {
    m.at = 'client';
    rec.step(LINE.client, '使用者拿到了包，一次 read() 结束。中间套了几层、每层做了什么，它都不知道。', { hl: { client: 'ok' }, note: note });
  }

  function goodPacket(m, p, rec) {
    reset(m, 48);
    rec.step(4, '从里往外套了三层：Stats(Logging(Validate(MemorySource)))。前面已经读了 48 个包。');
    down(m, rec, 1, true);
    at(m, rec, 'Validate'); at(m, rec, 'Logging'); at(m, rec, 'Stats');
    done(m, rec, '进去时从外往里：Stats → Logging → Validate → MemorySource；包回来时从里往外');
  }

  function badPacket(m, p, rec) {
    reset(m, 59);
    rec.step(4, '还是这条链，下一个是第 59 个包——它是个空包。');
    down(m, rec, 1, true);
    at(m, rec, 'Validate');
    down(m, rec, 4);
    at(m, rec, 'Validate'); at(m, rec, 'Logging'); at(m, rec, 'Stats');
    done(m, rec, '坏包在 Validate 这一层就被丢掉了，外面的 Logging 和 Stats 根本没见过它');
  }

  function swapped(m, p, rec) {
    reset(m, 59, ['Validate', 'Stats', 'Logging', 'Src']);
    rec.step(4, '同样四个类，只把 Validate 换到最外面：Validate(Stats(Logging(MemorySource)))。再读第 59 个包。');
    down(m, rec, 1, true);
    at(m, rec, 'Logging'); at(m, rec, 'Stats'); at(m, rec, 'Validate');
    down(m, rec, 2);
    at(m, rec, 'Logging'); at(m, rec, 'Stats'); at(m, rec, 'Validate');
    done(m, rec, '为了交出一个好包，Stats 数了两次。读完整个文件，这个顺序数到 240 个包，原来的顺序是 236 个');
  }

  DSViz.register('dp-decorator', {
    title: '装饰器：一次 read() 穿过每一层，包装顺序会改变结果',
    width: 760,
    height: 174,
    code: [
      'auto src = make_unique<MemorySource>();',
      'src = make_unique<Validate>(move(src));',
      'src = make_unique<Logging>(move(src));',
      'src = make_unique<Stats>(move(src));',
      'src->read(p);         // 使用者只认识 PacketSource',
      'Stats::read(p)        { inner_->read(p); ++packets_; }',
      'Logging::read(p)      { inner_->read(p); if (p.key) log(p); }',
      'Validate::read(p)     { while (inner_->read(p)) if (!p.data.empty()) return true; }',
      'MemorySource::read(p) { 生成第 i 个包 }'
    ],
    init: function () { var m = {}; reset(m, 48); return m; },
    view: view,
    start: function (m, rec) { goodPacket(m, {}, rec); },
    actions: [{ label: '读一个好包', run: goodPacket }, { label: '遇到坏包', run: badPacket }, { label: '换个顺序', run: swapped }]
  });
})();
