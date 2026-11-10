/* 设计模式专栏《组合》：递归计算 size() 时，调用怎样一层层展开、结果怎样一层层返回（dp-composite） */
(function () {
  'use strict';
  var ROW = 30, TOP = 40, X0 = 16, IND = 26, W = 200, SX = 420, SW = 300;
  // 树：name、kind（file / folder / archive）、bytes（KB）、depth、parent
  var NODES = [
    { id: 'project', name: 'project/', kind: 'folder', depth: 0 },
    { id: 'readme', name: 'README.md', kind: 'file', kb: 2, depth: 1, parent: 'project' },
    { id: 'src', name: 'src/', kind: 'folder', depth: 1, parent: 'project' },
    { id: 'main', name: 'main.cpp', kind: 'file', kb: 12, depth: 2, parent: 'src' },
    { id: 'player', name: 'player.cpp', kind: 'file', kb: 30, depth: 2, parent: 'src' },
    { id: 'assets', name: 'assets/', kind: 'folder', depth: 1, parent: 'project' },
    { id: 'logo', name: 'logo.png', kind: 'file', kb: 48, depth: 2, parent: 'assets' },
    { id: 'zip', name: 'logs.zip', kind: 'archive', kb: 1024, depth: 2, parent: 'assets' },
    { id: 'dec', name: 'decode.log', kind: 'file', kb: 6144, depth: 3, parent: 'zip' },
    { id: 'enc', name: 'encode.log', kind: 'file', kb: 4096, depth: 3, parent: 'zip' }
  ];
  var BY = {};
  NODES.forEach(function (n, i) { n.row = i; BY[n.id] = n; });
  function children(id) { return NODES.filter(function (n) { return n.parent === id; }); }
  var CLS = { folder: 'Folder', file: 'File', archive: 'Archive' };

  function view(m, x) {
    x = x || {};
    var boxes = [], arrows = [], texts = [];
    texts.push({ id: 'h1', x: X0, y: 22, text: '组合结构（每一行是一个 Component）', cls: 'muted' });
    texts.push({ id: 'h2', x: SX, y: 22, text: '调用栈（新的帧压在下面）', cls: 'muted' });
    NODES.forEach(function (n) {
      var r = m.result[n.id];
      var sub = r != null ? '= ' + r + ' KB' : (n.kind === 'file' ? n.kb + ' KB' : n.kind === 'archive' ? '压缩包 ' + n.kb + ' KB' : '');
      var hl = m.cur === n.id ? 'cur' : r != null ? 'ok' : m.skipped[n.id] ? 'empty' : null;
      boxes.push({ id: n.id, x: X0 + n.depth * IND, y: TOP + n.row * ROW, w: W, h: 24, text: n.name + (sub ? '  ' + sub : ''), hl: hl });
    });
    m.stack.forEach(function (f, i) {
      var n = BY[f.id];
      boxes.push({ id: 'f' + i, x: SX, y: TOP + i * 40, w: SW, h: 32,
        text: CLS[n.kind] + '::size()  ' + n.name + (n.kind === 'folder' ? '  sum = ' + f.sum + ' KB' : ''),
        hl: i === m.stack.length - 1 ? 'cur' : null });
    });
    if (m.stack.length) {
      var top = m.stack[m.stack.length - 1];
      arrows.push({ from: 'f' + (m.stack.length - 1), to: top.id, hl: 'cur' });
    }
    texts.push({ id: 'depth', x: SX, y: TOP + 4 * 40 + 20, text: '栈深度 ' + m.stack.length + '，最深到过 ' + m.maxDepth + ' 层', cls: 'muted' });
    if (x.note) texts.push({ id: 'note', x: SX, y: TOP + 4 * 40 + 44, text: x.note, cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts };
  }

  function reset(m) { m.result = {}; m.skipped = {}; m.stack = []; m.cur = null; m.maxDepth = 0; }

  function size(m, rec, id) {
    var n = BY[id];
    m.stack.push({ id: id, sum: 0 });
    m.maxDepth = Math.max(m.maxDepth, m.stack.length);
    m.cur = id;
    var frame = m.stack[m.stack.length - 1], v;
    if (n.kind === 'file') {
      v = n.kb;
      rec.step(7, n.name + ' 是叶子：File::size() 直接返回自己的大小 ' + v + ' KB。');
    } else if (n.kind === 'archive') {
      v = n.kb;
      children(id).forEach(function (c) { m.skipped[c.id] = true; });
      rec.step(8, n.name + ' 是压缩包：Archive 重写了 size()，返回自己的 ' + v + ' KB，不进入里面的两个文件。调用它的 Folder 并不知道这件事。');
    } else {
      rec.step(2, '进入 ' + n.name + ' 的 Folder::size()：栈上多了一帧，sum = 0。');
      children(id).forEach(function (c) {
        m.cur = c.id;
        rec.step(4, '对子节点 ' + c.name + ' 调用 c->size()。Folder 不判断它是文件还是文件夹，交给虚函数。');
        var r = size(m, rec, c.id);
        frame.sum += r;
        m.cur = id;
        rec.step(4, c.name + ' 返回 ' + r + ' KB，' + n.name + ' 的 sum 变成 ' + frame.sum + ' KB。');
      });
      v = frame.sum;
    }
    m.stack.pop();
    m.result[id] = v;
    m.cur = m.stack.length ? m.stack[m.stack.length - 1].id : null;
    if (n.kind === 'folder') rec.step(5, n.name + ' 返回 ' + v + ' KB，这一帧出栈。');
    return v;
  }

  function run(m, rec, id) {
    reset(m);
    var v = size(m, rec, id);
    rec.step(6, BY[id].name + '.size() = ' + v + ' KB。递归的深度等于树的深度：这棵树很浅，栈最深 ' + m.maxDepth + ' 层。',
      { note: '一条 13 万层的链就会栈溢出（第 6 节）' });
  }

  DSViz.register('dp-composite', {
    title: '递归计算 size()：调用一层层展开，结果一层层返回',
    width: 740,
    height: 350,
    code: [
      'long long Folder::size() const {',
      '    long long sum = 0;',
      '    for (const auto& c : children_)',
      '        sum += c->size();          // 虚函数：文件、文件夹、压缩包各算各的',
      '    return sum;',
      '}',
      'long long File::size() const { return bytes_; }',
      'long long Archive::size() const { return bytes_; }   // 不进入子节点'
    ],
    init: function () { var m = {}; reset(m); return m; },
    view: view,
    start: function (m, rec) { run(m, rec, 'project'); },
    actions: [
      { label: 'project.size()', run: function (m, p, rec) { run(m, rec, 'project'); } },
      { label: 'assets.size()', run: function (m, p, rec) { run(m, rec, 'assets'); } },
      { label: 'readme.size()', run: function (m, p, rec) { run(m, rec, 'readme'); } }
    ]
  });
})();
