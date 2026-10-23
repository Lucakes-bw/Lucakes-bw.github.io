/* 《复杂度分析》：顺序查找和二分查找的比较次数 */
(function () {
  'use strict';
  var N = 16;

  function view(m, x) {
    var cells = m.a.map(function (v, i) { return { v: v, hl: (x.hl || {})[i] || (x.dim && x.dim(i) ? 'dim' : null) }; });
    return { kind: 'array', cells: cells, cellW: 40, top: 70, ptrs: x.ptrs || [], label: x.label, vars: x.vars || {} };
  }

  DSViz.register('cx-search', {
    title: '在 16 个有序的数中查找：顺序查找 vs 二分查找',
    height: 150,
    init: function () {
      var a = [];
      for (var i = 0; i < N; i++) a.push(i * 3 + 2);
      return { a: a };
    },
    view: view,
    start: function (m, rec) {
      rec.step(0, '数组是有序的。输入要查找的数，分别用两种方法查找，比较它们的比较次数。', { label: '下标从 0 到 15' });
    },
    actions: [{
      label: '顺序查找',
      fields: [{ key: 'key', label: '查找', value: 41, size: 4 }],
      check: function (m, p) { return Number.isInteger(p.key) ? null : '请输入整数'; },
      run: function (m, p, rec) {
        var cmp = 0;
        for (var i = 0; i < N; i++) {
          cmp++;
          var hl = {}; hl[i] = 'cmp';
          var found = m.a[i] === p.key;
          if (found) hl[i] = 'ok';
          rec.step(0, '比较 a[' + i + '] = ' + m.a[i] + (found ? '，找到了！' : '，不是 ' + p.key + '，看下一个。'),
            { hl: hl, ptrs: [{ name: 'i', at: i }], dim: function (j) { return j < i; }, vars: { '比较次数': cmp } });
          if (found) {
            rec.step(0, '顺序查找一共比较了 ' + cmp + ' 次。最坏情况下（要找的数在最后或者不存在）要比较 n 次：O(n)。', { hl: hl, vars: { '比较次数': cmp } });
            return;
          }
        }
        rec.step(0, '找遍了 16 个元素都没有找到，比较了 ' + cmp + ' 次：这就是最坏情况，O(n)。', { vars: { '比较次数': cmp }, dim: function () { return true; } });
      }
    }, {
      label: '二分查找',
      fields: [{ key: 'key', label: '查找', value: 41, size: 4 }],
      check: function (m, p) { return Number.isInteger(p.key) ? null : '请输入整数'; },
      run: function (m, p, rec) {
        var lo = 0, hi = N - 1, cmp = 0;
        function dim(lo, hi) { return function (j) { return j < lo || j > hi; }; }
        rec.step(0, '查找范围是整个数组：lo = 0，hi = 15。', { ptrs: [{ name: 'lo', at: lo }, { name: 'hi', at: hi }], dim: dim(lo, hi), vars: { lo: lo, hi: hi, '比较次数': 0 } });
        while (lo <= hi) {
          var mid = lo + Math.floor((hi - lo) / 2);
          cmp++;
          var hl = {}; hl[mid] = 'cmp';
          var ptrs = [{ name: 'lo', at: lo }, { name: 'mid', at: mid }, { name: 'hi', at: hi }];
          if (m.a[mid] === p.key) {
            hl[mid] = 'ok';
            rec.step(0, '比较中间的 a[' + mid + '] = ' + m.a[mid] + '，找到了！二分查找一共比较了 ' + cmp + ' 次。每次比较都排除一半，16 个数最多比较 5 次：O(log n)。',
              { hl: hl, ptrs: ptrs, dim: dim(lo, hi), vars: { lo: lo, mid: mid, hi: hi, '比较次数': cmp } });
            return;
          }
          var go = m.a[mid] < p.key;
          rec.step(0, '比较中间的 a[' + mid + '] = ' + m.a[mid] + (go ? ' < ' : ' > ') + p.key + '，要找的数只可能在' + (go ? '右' : '左') + '半边，另一半全部排除。',
            { hl: hl, ptrs: ptrs, dim: dim(lo, hi), vars: { lo: lo, mid: mid, hi: hi, '比较次数': cmp } });
          if (go) lo = mid + 1; else hi = mid - 1;
          if (lo <= hi) rec.step(0, '缩小范围：lo = ' + lo + '，hi = ' + hi + '，还剩 ' + (hi - lo + 1) + ' 个数。',
            { ptrs: [{ name: 'lo', at: lo }, { name: 'hi', at: hi }], dim: dim(lo, hi), vars: { lo: lo, hi: hi, '比较次数': cmp } });
        }
        rec.step(0, 'lo > hi，范围为空，没有找到。只比较了 ' + cmp + ' 次就确定了结果。', { dim: function () { return true; }, vars: { lo: lo, hi: hi, '比较次数': cmp } });
      }
    }]
  });
})();
