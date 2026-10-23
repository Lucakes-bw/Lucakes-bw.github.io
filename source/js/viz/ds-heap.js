/* 《堆与优先队列》：上浮、下沉、O(n) 建堆；同时显示树和数组两种视图 */
(function () {
  'use strict';
  var MAX = 15;

  function make(vals) { var m = { a: [], nextId: 1 }; vals.forEach(function (v) { m.a.push({ id: m.nextId++, v: v }); }); return m; }
  function view(m, x) {
    var n = m.a.length, nodes = [];
    for (var i = 0; i < n; i++) {
      var l = 2 * i + 1, r = l + 1;
      nodes.push({ id: m.a[i].id, val: m.a[i].v, left: l < n ? m.a[l].id : null, right: r < n ? m.a[r].id : null, hl: (x.hl || {})[i], sub: i });
    }
    var cells = m.a.map(function (e, i) { return { v: e.v, hl: (x.hl || {})[i] }; });
    if (x.limit != null) cells.forEach(function (c, i) { if (i >= x.limit && !c.hl) c.hl = 'dim'; });
    return {
      kind: 'multi', vars: x.vars || { size: n },
      parts: [
        { kind: 'tree', nodes: nodes, root: n ? m.a[0].id : null, treeWidth: 700, top: 34, treeHeight: 196, maxGap: 46, maxDy: 50, ptrs: [] },
        { kind: 'array', x0: 20, cellW: 44, top: 252, cells: cells, cap: MAX, label: '数组：下标 i 的孩子是 2i+1 和 2i+2，父节点是 (i-1)/2', ptrs: x.ptrs || [] }
      ]
    };
  }
  function hl(o) { return o; }
  function pair(a, b, ca, cb) { var o = {}; o[a] = ca; if (b != null) o[b] = cb; return o; }
  function swap(m, i, j) { var t = m.a[i]; m.a[i] = m.a[j]; m.a[j] = t; }

  function siftUp(m, i, rec) {
    while (i > 0) {
      var p = Math.floor((i - 1) / 2);
      rec.step(5, '比较 a[' + i + '] = ' + m.a[i].v + ' 和它的父节点 a[' + p + '] = ' + m.a[p].v + '。', { hl: pair(i, p, 'cur', 'cmp'), ptrs: [{ name: 'i', at: i }] });
      if (m.a[p].v >= m.a[i].v) { rec.step(5, '父节点不小于它，满足堆的性质，停止上浮。', { hl: pair(i, null, 'ok') }); return; }
      swap(m, i, p);
      rec.step(6, '它比父节点大，交换，上浮一层。', { hl: pair(p, i, 'cur', 'cmp'), ptrs: [{ name: 'i', at: p }] });
      i = p;
    }
    rec.step(3, '已经到达堆顶，停止。', { hl: pair(0, null, 'ok') });
  }
  function siftDown(m, n, i, rec, limitView) {
    for (;;) {
      var l = 2 * i + 1, r = l + 1, big = i;
      if (l < n && m.a[l].v > m.a[big].v) big = l;
      if (r < n && m.a[r].v > m.a[big].v) big = r;
      var o = {}; o[i] = 'cur'; if (l < n) o[l] = 'cmp'; if (r < n) o[r] = 'cmp';
      rec.step(16, '比较 a[' + i + '] = ' + m.a[i].v + ' 和它的孩子' + (l < n ? ' a[' + l + '] = ' + m.a[l].v : '') + (r < n ? '、a[' + r + '] = ' + m.a[r].v : '') + (l >= n ? '：它没有孩子' : '') + '。', { hl: o, ptrs: [{ name: 'i', at: i }], limit: limitView });
      if (big === i) { rec.step(17, '它不小于两个孩子，停止下沉。', { hl: pair(i, null, 'ok'), limit: limitView }); return; }
      swap(m, i, big);
      rec.step(18, '和较大的孩子交换，下沉一层。', { hl: pair(big, i, 'cur', 'cmp'), ptrs: [{ name: 'i', at: big }], limit: limitView });
      i = big;
    }
  }

  DSViz.register('heap-ops', {
    title: '大顶堆：树和数组是同一份数据的两种画法',
    height: 318,
    code: [
      'static void siftUp(int* a, size_t i)',
      '{',
      '    while (i > 0) {',
      '        size_t parent = (i - 1) / 2;',
      '        if (a[parent] >= a[i]) break;',
      '        swap(&a[parent], &a[i]);',
      '        i = parent;',
      '    }',
      '}',
      '',
      'static void siftDown(int* a, size_t n, size_t i)',
      '{',
      '    for (;;) {',
      '        size_t left = 2 * i + 1, right = left + 1, largest = i;',
      '        if (left < n && a[left] > a[largest]) largest = left;',
      '        if (right < n && a[right] > a[largest]) largest = right;',
      '        if (largest == i) break;',
      '        swap(&a[i], &a[largest]);',
      '        i = largest;',
      '    }',
      '}',
      '',
      'void heapify(int* a, size_t n)',
      '{',
      '    for (size_t i = n / 2; i-- > 0; ) siftDown(a, n, i);',
      '}'
    ],
    init: function () { return make([60, 40, 50, 10, 20, 30, 5]); },
    view: view,
    start: function (m, rec) { rec.step(0, '每个节点都不小于它的孩子，所以堆顶是最大值。树上节点下方的小数字是它在数组中的下标。试试入堆 55，或者出堆。'); },
    actions: [{
      label: '入堆',
      fields: [{ key: 'value', label: 'value', value: 55, size: 4 }],
      check: function (m, p) {
        if (!Number.isInteger(p.value) || Math.abs(p.value) > 999) return '请输入 -999 到 999 之间的整数';
        return m.a.length >= MAX ? '演示最多 ' + MAX + ' 个元素' : null;
      },
      run: function (m, p, rec) {
        m.a.push({ id: m.nextId++, v: p.value });
        var i = m.a.length - 1;
        rec.step(0, '新元素 ' + p.value + ' 先放在数组的最后（树的最后一层最靠右的位置），保持完全二叉树的形状。', { hl: pair(i, null, 'new'), ptrs: [{ name: 'i', at: i }] });
        siftUp(m, i, rec);
        rec.step(0, '入堆完成。上浮最多经过树的高度那么多层：O(log n)。');
      }
    }, {
      label: '出堆',
      run: function (m, p, rec) {
        if (!m.a.length) { rec.step(0, '堆是空的。'); return; }
        var top = m.a[0].v;
        rec.step(0, '堆顶 ' + top + ' 是最大值，把它取出。', { hl: pair(0, null, 'del') });
        var last = m.a.pop();
        if (!m.a.length) { rec.step(0, '取出后堆为空。'); return; }
        m.a[0] = last;
        rec.step(0, '把最后一个元素 ' + last.v + ' 移到堆顶，形状仍然是完全二叉树，但它可能比孩子小。', { hl: pair(0, null, 'new') });
        siftDown(m, m.a.length, 0, rec);
        rec.step(0, '出堆完成，取出的是 ' + top + '，新的堆顶是 ' + m.a[0].v + '。O(log n)。');
      }
    }, {
      label: '用 heapify 建堆',
      fields: [{ key: 'list', label: '数组', value: '4 10 3 5 1 8 9 2', size: 18 }],
      check: function (m, p) {
        var a = String(p.list).split(/[\s,，]+/).filter(Boolean).map(Number);
        if (!a.length || !a.every(Number.isInteger)) return '请输入用空格分隔的整数';
        return a.length > MAX ? '最多 ' + MAX + ' 个数' : null;
      },
      run: function (m, p, rec) {
        var vals = String(p.list).split(/[\s,，]+/).filter(Boolean).map(Number);
        var fresh = make(vals);
        m.a = fresh.a; m.nextId = fresh.nextId;
        var n = m.a.length;
        rec.step(23, '原始数组还不是堆。叶子节点（下标 ' + Math.floor(n / 2) + ' 及以后）自己就是合法的堆，所以从最后一个非叶子节点 ' + (Math.floor(n / 2) - 1) + ' 开始，倒着对每个节点下沉。');
        for (var i = Math.floor(n / 2) - 1; i >= 0; i--) {
          rec.step(25, '对下标 ' + i + '（' + m.a[i].v + '）下沉：此时它的两棵子树都已经是堆了。', { hl: pair(i, null, 'cur'), ptrs: [{ name: 'i', at: i }] });
          siftDown(m, n, i, rec);
        }
        rec.step(26, 'heapify 完成：' + m.a.map(function (e) { return e.v; }).join(' ') + '。总的交换次数是 O(n)，比逐个插入的 O(n log n) 更快。');
      }
    }]
  });
})();
