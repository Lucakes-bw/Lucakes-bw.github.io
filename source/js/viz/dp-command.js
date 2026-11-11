/* 设计模式专栏《命令》：撤销栈和重做栈怎么动——执行、撤销、重做、新命令清空重做栈、拖动合并、宏命令倒着撤销（dp-command） */
(function () {
  'use strict';

  var X0 = 20, PX = 660 / 12000;        // 时间线 0～12 s 画在 660 像素宽的轨道上
  var SX = [20, 370], SY = 172, SH = 30, SW = 320;

  function clipText(c) {
    var s = '音量 ' + c.vol.toFixed(1);
    if (c.filters.length) s += ' · ' + c.filters.join(',');
    return s;
  }

  function view(m, x) {
    x = x || {};
    var hl = x.hl || {}, boxes = [], texts = [];
    texts.push({ id: 'tt', x: X0, y: 20, text: 'Timeline（接收者）', cls: 'muted' });
    m.clips.forEach(function (c) {
      boxes.push({ id: 'c' + c.id, x: X0 + c.start * PX, y: 32, w: Math.max(40, c.len * PX - 4), h: 40,
        text: '#' + c.id, sub: clipText(c), hl: hl['c' + c.id] });
    });
    for (var s = 0; s <= 12; s += 2)
      texts.push({ id: 'r' + s, x: X0 + s * 1000 * PX, y: 116, text: s + 's', cls: 'muted' });
    texts.push({ id: 'td', x: SX[0], y: 154, text: '撤销栈 done_（上面是栈顶）', cls: 'muted' });
    texts.push({ id: 'tu', x: SX[1], y: 154, text: '重做栈 undone_（上面是栈顶）', cls: 'muted' });
    [m.done, m.undone].forEach(function (stack, k) {
      if (!stack.length) boxes.push({ id: 'empty' + k, x: SX[k], y: SY, w: SW, h: SH, text: '（空）', hl: hl['empty' + k] });
      stack.forEach(function (cmd, i) {
        boxes.push({ id: 'k' + cmd.uid, x: SX[k], y: SY + (stack.length - 1 - i) * (SH + 6), w: SW, h: SH,
          text: cmd.text(), hl: hl['k' + cmd.uid] });
      });
    });
    if (x.note) texts.push({ id: 'note', x: X0, y: 376, text: x.note, cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: [], texts: texts };
  }

  var uid = 0;
  function find(m, id) { for (var i = 0; i < m.clips.length; i++) if (m.clips[i].id === id) return i; return -1; }

  // ---- 几种命令：execute / undo 都只改 m.clips ----
  function Move(id, to) {
    var from = 0, c = { uid: ++uid, kind: 'move', id: id, to: to };
    c.exec = function (m) { var k = m.clips[find(m, id)]; from = k.start; k.start = c.to; };
    c.undo = function (m) { m.clips[find(m, id)].start = from; };
    c.text = function () { return '移动 #' + id + '  ' + from + '→' + c.to + 'ms'; };
    c.merge = function (n) { if (n.kind !== 'move' || n.id !== id || !c.drag) return false; c.to = n.to; return true; };
    return c;
  }
  function Volume(id, v) {
    var old = 0, c = { uid: ++uid };
    c.exec = function (m) { var k = m.clips[find(m, id)]; old = k.vol; k.vol = v; };
    c.undo = function (m) { m.clips[find(m, id)].vol = old; };
    c.text = function () { return '调音量 #' + id + '  ' + old.toFixed(1) + '→' + v.toFixed(1); };
    return c;
  }
  function Filter(id, f) {
    var c = { uid: ++uid };
    c.exec = function (m) { m.clips[find(m, id)].filters.push(f); };
    c.undo = function (m) { m.clips[find(m, id)].filters.pop(); };
    c.text = function () { return '加滤镜 #' + id + '  ' + f; };
    return c;
  }
  function Split(id, at, nid) {
    var c = { uid: ++uid };
    c.exec = function (m) {
      var i = find(m, id), l = m.clips[i];
      var r = { id: nid, start: at, len: l.start + l.len - at, vol: l.vol, filters: l.filters.slice() };
      l.len = at - l.start;
      m.clips.splice(i + 1, 0, r);
    };
    c.undo = function (m) { var r = find(m, nid); m.clips[find(m, id)].len += m.clips[r].len; m.clips.splice(r, 1); };
    c.text = function () { return '剪切 #' + id + ' @' + at + 'ms → #' + nid; };
    return c;
  }
  function Delete(id) {
    var saved, index, c = { uid: ++uid };
    c.exec = function (m) { index = find(m, id); saved = m.clips[index]; m.clips.splice(index, 1); };
    c.undo = function (m) { m.clips.splice(index, 0, saved); };
    c.text = function () { return '删除 #' + id; };
    return c;
  }

  function reset(m) {
    m.clips = [
      { id: 1, start: 0, len: 3000, vol: 1, filters: [] },
      { id: 2, start: 3000, len: 5000, vol: 1, filters: [] },
      { id: 3, start: 8000, len: 2000, vol: 1, filters: [] }
    ];
    m.done = []; m.undone = [];
  }

  function hlOf(c, extra) { var h = {}; h['k' + c.uid] = 'new'; for (var k in extra) h[k] = extra[k]; return h; }

  function run(m, c, rec, text, clipHl, note) {
    c.exec(m);
    var cleared = m.undone.length > 0;
    m.undone = [];
    m.done.push(c);
    var h = hlOf(c, clipHl);
    if (cleared) h.empty1 = 'warn';
    rec.step(cleared ? 3 : 5, text, { hl: h, note: note });
  }
  function undo(m, rec, text, clipHl, note) {
    var c = m.done.pop();
    c.undo(m);
    m.undone.push(c);
    var h = { }; h['k' + c.uid] = 'cur'; for (var k in clipHl) h[k] = clipHl[k];
    rec.step(7, text, { hl: h, note: note });
  }
  function redo(m, rec, text, clipHl) {
    var c = m.undone.pop();
    c.exec(m);
    m.done.push(c);
    var h = { }; h['k' + c.uid] = 'ok'; for (var k in clipHl) h[k] = clipHl[k];
    rec.step(8, text, { hl: h });
  }

  function basic(m, p, rec) {
    reset(m);
    rec.step(1, '三个片段都剪自 bbb-10s.mp4。History 持有两个栈：做过的命令在撤销栈，撤销掉的在重做栈。');
    run(m, Move(3, 9000), rec, 'run(MoveClip(#3, 9000))：execute 时先记下旧位置 8000，再改成 9000，命令压进撤销栈。', { c3: 'cmp' });
    run(m, Volume(2, 0.5), rec, '调音量：命令里记下旧音量 1.0。每个命令只存自己改过的那一点，不存整条时间线。', { c2: 'cmp' });
    run(m, Split(2, 5000, 4), rec, '在 5 s 处剪开 #2：后半段的编号 #4 在构造命令时就定好，重做时还是 #4，后面引用 #4 的命令才不会找不到。', { c2: 'cmp', c4: 'new' });
    undo(m, rec, 'Ctrl+Z：弹出栈顶的「剪切」，调用它的 undo()：删掉 #4、把 #2 恢复成 5 s 长，然后压进重做栈。', { c2: 'ok' });
    undo(m, rec, '再撤销：「调音量」的 undo() 用记下的旧值把音量改回 1.0。撤销总是从最近的一条开始，倒着来。', { c2: 'ok' });
    redo(m, rec, 'Ctrl+Shift+Z：从重做栈弹出「调音量」，再调用一次 execute()，压回撤销栈。', { c2: 'cmp' });
  }

  function branch(m, p, rec) {
    reset(m);
    run(m, Move(3, 9000), rec, '先做两条命令。', { c3: 'cmp' });
    run(m, Filter(1, 'fade'), rec, '给 #1 加一个淡入滤镜。', { c1: 'cmp' });
    undo(m, rec, '撤销「加滤镜」，它进了重做栈，这时还能重做。', { c1: 'ok' });
    run(m, Volume(1, 0.8), rec, '撤销之后做了一条新命令：重做栈被清空。被撤销的「加滤镜」是基于旧状态记下的，新命令之后再重做它，结果没有意义。', { c1: 'cmp' },
      '历史是一条线，不是一棵树：撤销后做新操作，被撤销的那一支就丢了');
  }

  function drag(m, p, rec) {
    reset(m);
    rec.step(1, '按下鼠标拖 #3，每来一个 mousemove 事件就产生一条 MoveClip。同一次拖动里的命令带同一个拖动编号。');
    var first = Move(3, 8300); first.drag = true;
    run(m, first, rec, '第一条：8000 → 8300，压栈。', { c3: 'cmp' });
    [8600, 8900, 9200].forEach(function (to) {
      var c = Move(3, to); c.drag = true;
      c.exec(m);
      m.undone = [];
      m.done[m.done.length - 1].merge(c);
      var h = { c3: 'cmp' }; h['k' + first.uid] = 'cur';
      rec.step(4, '又一条移动到 ' + to + 'ms：先 execute，再问栈顶能不能 mergeWith 它——同一个片段、同一次拖动，能。栈顶只把终点改成 ' + to + '，起点还是 8000，新命令丢掉。', { hl: h });
    });
    undo(m, rec, '松开鼠标，再按一次 Ctrl+Z：一下回到 8000。不合并的话，这次拖动会在撤销栈里留下上百条，要按上百次。', { c3: 'ok' },
      '实测：120 个 mousemove，不合并 120 条，合并后 1 条');
  }

  function macro(m, p, rec) {
    reset(m);
    m.clips.push({ id: 4, start: 10000, len: 1500, vol: 1, filters: [] });
    rec.step(1, '波纹删除 #2：删掉它，后面的 #3、#4 往前挪 5 s 补上空位。这是三条命令，但对用户是一次操作。');
    var subs = [Delete(2), Move(3, 3000), Move(4, 5000)];
    var mc = { uid: ++uid, exec: function (mm) { subs.forEach(function (s) { s.exec(mm); }); },
      undo: function (mm) { for (var i = subs.length - 1; i >= 0; i--) subs[i].undo(mm); },
      text: function () { return '波纹删除 #2（宏：3 条）'; } };
    run(m, mc, rec, 'MacroCommand 本身也是一个 Command，里面按顺序装着「删除 #2」「移动 #3」「移动 #4」；整个宏只占撤销栈的一格。', { c3: 'cmp', c4: 'cmp' });
    m.done.pop();
    var h0 = {}; h0['k' + mc.uid] = 'cur';
    m.done.push(mc);
    subs[2].undo(m);
    rec.step(9, 'Ctrl+Z 撤销宏：倒着撤销里面的命令。先撤最后一条「移动 #4」……', { hl: Object.assign({ c4: 'ok' }, h0) });
    subs[1].undo(m);
    rec.step(9, '……再撤「移动 #3」……', { hl: Object.assign({ c3: 'ok' }, h0) });
    subs[0].undo(m);
    m.done.pop(); m.undone.push(mc);
    var h1 = { c2: 'new' }; h1['k' + mc.uid] = 'cur';
    rec.step(9, '……最后把 #2 插回原来的位置。宏整体进了重做栈。如果正着撤，前面的命令会在后面的命令改过的状态上恢复，结果就乱了。', { hl: h1 });
  }

  DSViz.register('dp-command', {
    title: '撤销栈和重做栈：命令怎么在两个栈之间移动',
    width: 720,
    height: 390,
    code: [
      'void History::run(unique_ptr<Command> c) {',
      '  c->execute(tl_);',
      '  undone_.clear();                  // 做了新命令，重做栈作废',
      '  if (!done_.empty() && done_.back()->mergeWith(*c)) return;',
      '  done_.push_back(std::move(c));',
      '}',
      'void History::undo() { done_.back()->undo(tl_);    undone_.push_back(弹出 done_ 栈顶); }',
      'void History::redo() { undone_.back()->execute(tl_); done_.push_back(弹出 undone_ 栈顶); }',
      'void MacroCommand::undo(Timeline& tl) { for (倒着遍历 cmds_) c->undo(tl); }'
    ],
    init: function () { var m = {}; reset(m); return m; },
    view: view,
    start: function (m, rec) { basic(m, {}, rec); },
    actions: [
      { label: '执行、撤销、重做', run: basic },
      { label: '撤销后做新命令', run: branch },
      { label: '拖动合并', run: drag },
      { label: '宏命令', run: macro }
    ]
  });
})();
