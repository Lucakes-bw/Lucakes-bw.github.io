/* 《线性时间排序与排序总结》：计数排序的三个阶段 */
(function () {
  'use strict';
  var K = 10;

  function parse(s) { var a = String(s).split(/[\s,，]+/).filter(Boolean).map(Number); return a.length && a.every(Number.isInteger) ? a : null; }
  function row(vals, x0, top, label, hl, index, w) {
    return { kind: 'array', x0: x0, cellW: w || 44, top: top, label: label, noIndex: !index, cap: vals.length, ptrs: [],
      cells: vals.map(function (v, i) { return { v: v == null ? '' : v, hl: (hl || {})[i] || (v == null ? 'empty' : null) }; }) };
  }
  function view(m, x) {
    return {
      kind: 'multi', vars: x.vars || {},
      parts: [row(m.a, 20, 60, '输入 a', x.aHl, true), row(x.count || [], 20, 192, 'count（下标是值，0 到 9）', x.cHl, true), row(x.out || m.a.map(function () { return null; }), 20, 324, '输出 out', x.oHl, true)]
    };
  }

  DSViz.register('counting-sort', {
    title: '计数排序：先数一数每个值出现了几次',
    height: 384,
    code: [
      'for (int i = 0; i < n; ++i) count[a[i]]++;',
      'for (int v = 1; v < k; ++v) count[v] += count[v - 1];',
      'for (int i = n - 1; i >= 0; --i) out[--count[a[i]]] = a[i];'
    ],
    init: function () { return { a: [3, 1, 4, 1, 5, 9, 2, 6, 5, 3] }; },
    view: view,
    start: function (m, rec) { rec.step(0, '所有的值都在 0 到 9 之间。计数排序完全不比较两个元素的大小。点击「开始」。'); },
    actions: [{
      label: '开始',
      fields: [{ key: 'list', label: '数组', value: '3 1 4 1 5 9 2 6 5 3', size: 22 }],
      check: function (m, p) {
        var a = parse(p.list);
        if (!a) return '请输入用空格分隔的整数';
        if (a.length < 2 || a.length > 13) return '请输入 2 到 13 个数';
        return a.some(function (v) { return v < 0 || v >= K; }) ? '每个数在 0 到 9 之间' : null;
      },
      run: function (m, p, rec) {
        m.a = parse(p.list);
        var a = m.a, n = a.length, count = [], out = a.map(function () { return null; });
        for (var v = 0; v < K; v++) count.push(0);
        rec.step(1, '第一步：统计每个值出现的次数。', { count: count.slice() });
        for (var i = 0; i < n; i++) {
          count[a[i]]++;
          var ah = {}; ah[i] = 'cur'; var ch = {}; ch[a[i]] = 'new';
          rec.step(1, 'a[' + i + '] = ' + a[i] + '，count[' + a[i] + '] 加 1。', { count: count.slice(), aHl: ah, cHl: ch });
        }
        rec.step(2, '第二步：求前缀和。之后 count[v] 表示「小于等于 v 的元素有几个」，也就是值为 v 的最后一个元素应该放在哪里（的下一个位置）。', { count: count.slice() });
        for (var w = 1; w < K; w++) {
          count[w] += count[w - 1];
          var c2 = {}; c2[w] = 'new'; c2[w - 1] = 'cmp';
          rec.step(2, 'count[' + w + '] += count[' + (w - 1) + ']，等于 ' + count[w] + '。', { count: count.slice(), cHl: c2 });
        }
        rec.step(3, '第三步：从后往前扫描输入，把每个元素放到它的位置上。', { count: count.slice(), out: out.slice() });
        for (var j = n - 1; j >= 0; j--) {
          var pos = --count[a[j]];
          out[pos] = a[j];
          var a2 = {}; a2[j] = 'cur'; var c3 = {}; c3[a[j]] = 'cmp'; var o3 = {}; o3[pos] = 'new';
          rec.step(3, 'a[' + j + '] = ' + a[j] + '：count[' + a[j] + '] 减 1 得 ' + pos + '，放到 out[' + pos + ']。从后往前放，值相同的元素保持原来的先后顺序（稳定）。', { count: count.slice(), out: out.slice(), aHl: a2, cHl: c3, oHl: o3 });
        }
        rec.step(3, '完成：' + out.join(' ') + '。总共扫描了输入两遍、count 一遍：O(n + k)。', { count: count.slice(), out: out.slice() });
      }
    }]
  });
})();
