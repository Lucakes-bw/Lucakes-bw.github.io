/* 《B+ 树索引》：在 B+ 树中按主键查找一行，以及范围查询沿叶子页的链表扫描 */
(function () {
  'use strict';
  var RX = 10, IX = 190, LX = 420, LW = 230, H = 34;
  var ROOT = { id: 'root', y: 140, keys: [5, 42] };
  var INNER = [{ id: 'a', y: 60, keys: [5, 15, 30], kids: [0, 1, 2] }, { id: 'b', y: 225, keys: [42, 55], kids: [3, 4] }];
  var LEAVES = [[5, 8, 12], [15, 20, 23], [30, 34, 38], [42, 47, 50], [55, 61, 66]];
  function leafY(i) { return 10 + i * 66; }

  function view(m, x) {
    x = x || {};
    var hl = x.hl || {}, boxes = [], arrows = [], texts = [];
    boxes.push({ id: 'root', x: RX, y: ROOT.y, w: 130, h: H, text: '根页：' + ROOT.keys.join(' | '), hl: hl.root });
    INNER.forEach(function (n) {
      boxes.push({ id: n.id, x: IX, y: n.y, w: 160, h: H, text: n.keys.join(' | '), hl: hl[n.id] });
      arrows.push({ from: 'root', to: n.id, hl: (x.path || []).indexOf(n.id) >= 0 ? 'cur' : null });
    });
    LEAVES.forEach(function (keys, i) {
      boxes.push({ id: 'l' + i, x: LX, y: leafY(i), w: LW, h: H, text: '叶子 ' + (i + 1) + '：' + keys.join('  '), hl: hl['l' + i] });
      if (i < LEAVES.length - 1) texts.push({ id: 'n' + i, x: LX + LW / 2, y: leafY(i) + H + 22, text: '↓ 下一页', cls: 'muted', anchor: 'middle' });
    });
    INNER.forEach(function (n) {
      n.kids.forEach(function (k) { arrows.push({ from: n.id, to: 'l' + k, hl: (x.path || []).indexOf('l' + k) >= 0 ? 'cur' : null }); });
    });
    texts.push({ id: 'hr', x: RX, y: ROOT.y - 10, text: '第 2 层', cls: 'muted' });
    texts.push({ id: 'hi', x: IX, y: 48, text: '第 1 层', cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts, vars: x.vars };
  }

  // 从根走到包含 key 的叶子，返回读过的页数
  function descend(rec, key, what) {
    var inner = key >= 42 ? INNER[1] : INNER[0];
    var idx = 0;
    inner.keys.forEach(function (k, i) { if (key >= k) idx = i; });
    var leaf = inner.kids[idx];
    rec.step(0, '从根页开始（读第 1 页）。根页中的 5 和 42 是两个子页各自的最小主键：' + key +
      (key >= 42 ? ' ≥ 42，去第二个子页。' : ' 在 5 和 42 之间，去第一个子页。'),
      { hl: { root: 'cur' }, path: [inner.id], vars: { '读取的页数': 1, '要找': what } });
    rec.step(0, '读第 1 层的页（第 2 页）。' + inner.keys.join('、') + ' 中，不大于 ' + key + ' 的最大一个是 ' + inner.keys[idx] +
      '，所以去它指向的叶子 ' + (leaf + 1) + '。',
      { hl: { root: null, a: inner.id === 'a' ? 'cur' : null, b: inner.id === 'b' ? 'cur' : null }, path: [inner.id, 'l' + leaf], vars: { '读取的页数': 2, '要找': what } });
    return leaf;
  }

  DSViz.register('bptree-search', {
    title: 'B+ 树：查找和范围扫描',
    height: 345,
    init: function () { return {}; },
    view: view,
    start: function (m, rec) { rec.step(0, '一棵 3 层的 B+ 树（为了画得下，每个叶子页只放 3 行）。点「查找 34」或「范围 20～47」。'); },
    actions: [
      { label: '查找 34', run: function (m, p, rec) {
        var leaf = descend(rec, 34, 'id = 34');
        var h = {}; h['l' + leaf] = 'new';
        rec.step(0, '读叶子 ' + (leaf + 1) + '（第 3 页），在页中用页目录二分查找，找到 34。叶子页中存的是完整的行，直接返回。一共只读了 3 个页。',
          { hl: h, path: [], vars: { '读取的页数': 3, '要找': 'id = 34' } });
      } },
      { label: '范围 20～47', run: function (m, p, rec) {
        var what = 'id BETWEEN 20 AND 47';
        var leaf = descend(rec, 20, what);
        var found = [], pages = 3;
        for (var i = leaf; i < LEAVES.length; i++) {
          var h = {}; h['l' + i] = 'cur';
          var got = LEAVES[i].filter(function (k) { return k >= 20 && k <= 47; });
          found = found.concat(got);
          var stop = LEAVES[i][LEAVES[i].length - 1] > 47;
          rec.step(0, (i === leaf ? '读叶子 ' + (i + 1) + '，找到起点 20。' : '沿「下一页」指针读叶子 ' + (i + 1) + '，不需要回到根页。') +
            '取出 ' + got.join('、') + '。' + (stop ? '遇到 50 > 47，扫描结束。' : '这一页取完了，继续下一页。'),
            { hl: h, path: i === leaf ? [] : [], vars: { '读取的页数': pages, '结果': found.join(', ') } });
          if (stop) break;
          pages++;
        }
        rec.step(0, '共读了 ' + pages + ' 个页：3 个用来找到起点，之后每个叶子页一次。叶子页按主键排好序并且连成链表，所以范围查询、ORDER BY 主键都很快。',
          { hl: {}, vars: { '读取的页数': pages, '结果': found.join(', ') } });
      } }
    ]
  });
})();
