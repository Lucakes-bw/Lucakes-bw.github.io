/* 《快速排序与归并排序》：Lomuto 划分的快速排序、自顶向下的归并排序 */
(function () {
  'use strict';

  function parse(s) { var a = String(s).split(/[\s,，]+/).filter(Boolean).map(Number); return a.length && a.every(Number.isInteger) ? a : null; }
  function load(m, list) { m.a = list.map(function (v, i) { return { id: i, v: v }; }); }
  function view(m, x) {
    var lo = x.lo == null ? -1 : x.lo, hi = x.hi == null ? -1 : x.hi;
    return {
      kind: 'bars', height: 240,
      values: m.a.map(function (e, i) {
        var h = (x.hl || {})[i];
        if (!h && x.done && x.done[e.id]) h = 'ok';
        if (!h && x.lo != null && (i < lo || i > hi)) h = 'dim';
        return { id: e.id, v: e.v, hl: h };
      }),
      ptrs: x.ptrs || [], vars: x.vars || {}
    };
  }
  function check(m, p) {
    var a = parse(p.list);
    if (!a) return '请输入用空格分隔的整数';
    if (a.length < 2 || a.length > 12) return '请输入 2 到 12 个数';
    return a.some(function (v) { return v < 0 || v > 99; }) ? '每个数在 0 到 99 之间' : null;
  }
  function swap(a, i, j) { var t = a[i]; a[i] = a[j]; a[j] = t; }

  DSViz.register('quicksort', {
    title: '快速排序：选一个基准，小的放左边，大的放右边，再分别递归',
    height: 240,
    code: [
      'static int partitionLomuto(int* a, int lo, int hi)',
      '{',
      '    int pivot = a[hi];',
      '    int i = lo;',
      '    for (int j = lo; j < hi; ++j) {',
      '        if (a[j] < pivot) swap(&a[i++], &a[j]);',
      '    }',
      '    swap(&a[i], &a[hi]);',
      '    return i;',
      '}',
      '',
      'void quickSortNaive(int* a, int lo, int hi)',
      '{',
      '    if (lo >= hi) return;',
      '    int p = partitionLomuto(a, lo, hi);',
      '    quickSortNaive(a, lo, p - 1);',
      '    quickSortNaive(a, p + 1, hi);',
      '}'
    ],
    init: function () { var m = {}; load(m, [5, 2, 9, 1, 7, 3, 8, 4]); return m; },
    view: view,
    start: function (m, rec) { rec.step(0, '每次以区间最后一个元素为基准（橙色边框），把比它小的换到左边。变暗的部分不在当前处理的区间内。可以试试已经有序的数据，看看会发生什么。'); },
    actions: [{
      label: '开始',
      fields: [{ key: 'list', label: '数组', value: '5 2 9 1 7 3 8 4', size: 20 }],
      check: check,
      run: function (m, p, rec) {
        load(m, parse(p.list));
        var a = m.a, done = {}, cmp = 0, depth = 0, maxDepth = 0;
        function v() { return { '比较': cmp, '递归深度': depth, '最大深度': maxDepth }; }
        function qs(lo, hi) {
          if (lo > hi) return;
          depth++; if (depth > maxDepth) maxDepth = depth;
          if (lo === hi) { done[a[lo].id] = true; rec.step(14, '区间 [' + lo + ', ' + hi + '] 只有一个元素，已经有序。', { lo: lo, hi: hi, done: done, vars: v() }); depth--; return; }
          var pivot = a[hi].v, i = lo, hl = {};
          hl[hi] = 'warn';
          rec.step(3, '处理区间 [' + lo + ', ' + hi + ']，基准 pivot = ' + pivot + '。', { lo: lo, hi: hi, hl: hl, done: done, ptrs: [{ name: 'i', at: i }], vars: v() });
          for (var j = lo; j < hi; ++j) {
            cmp++;
            var o = {}; o[hi] = 'warn'; o[j] = 'cmp';
            var less = a[j].v < pivot;
            rec.step(6, 'a[' + j + '] = ' + a[j].v + (less ? ' < ' + pivot + '，换到左边（位置 ' + i + '），i 加 1。' : ' 不小于 ' + pivot + '，留在右边。'), { lo: lo, hi: hi, hl: o, done: done, ptrs: [{ name: 'i', at: i }, { name: 'j', at: j }], vars: v() });
            if (less) { swap(a, i, j); i++; }
          }
          swap(a, i, hi);
          done[a[i].id] = true;
          var o2 = {}; o2[i] = 'ok';
          rec.step(8, '把基准 ' + pivot + ' 换到位置 ' + i + '：它左边的都比它小，右边的都不比它小，它已经在最终的位置上了。', { lo: lo, hi: hi, hl: o2, done: done, vars: v() });
          qs(lo, i - 1);
          qs(i + 1, hi);
          depth--;
        }
        qs(0, a.length - 1);
        rec.step(17, '快速排序完成：比较 ' + cmp + ' 次，最大递归深度 ' + maxDepth + '。' + (maxDepth >= a.length ? '每次的基准都是区间中最大（或最小）的元素，划分极不平衡，递归深度等于 n，比较了 n(n-1)/2 次：这是快速排序的最坏情况。' : '每次划分大致平衡时，递归深度约为 log n。'), { done: done, vars: v() });
      }
    }]
  });

  DSViz.register('mergesort', {
    title: '归并排序：先分别排好两半，再合并',
    height: 240,
    code: [
      'static void mergeSortRec(int* a, int* tmp, int lo, int hi)',
      '{',
      '    if (lo >= hi) return;',
      '    int mid = lo + (hi - lo) / 2;',
      '    mergeSortRec(a, tmp, lo, mid);',
      '    mergeSortRec(a, tmp, mid + 1, hi);',
      '    merge(a, tmp, lo, mid, hi);',
      '}',
      '',
      '/* merge：i 扫描左半，j 扫描右半，每次取较小的放入 tmp */',
      'while (i <= mid && j <= hi)',
      '    tmp[k++] = (a[i] <= a[j]) ? a[i++] : a[j++];'
    ],
    init: function () { var m = {}; load(m, [5, 2, 9, 1, 7, 3, 8, 4]); return m; },
    view: view,
    start: function (m, rec) { rec.step(0, '把数组一分为二，分别排序，再把两个有序的部分合并成一个。点击「开始」。'); },
    actions: [{
      label: '开始',
      fields: [{ key: 'list', label: '数组', value: '5 2 9 1 7 3 8 4', size: 20 }],
      check: check,
      run: function (m, p, rec) {
        load(m, parse(p.list));
        var a = m.a, cmp = 0;
        function v() { return { '比较': cmp }; }
        function ms(lo, hi) {
          if (lo >= hi) return;
          var mid = lo + Math.floor((hi - lo) / 2);
          rec.step(4, '把 [' + lo + ', ' + hi + '] 分成 [' + lo + ', ' + mid + '] 和 [' + (mid + 1) + ', ' + hi + ']。', { lo: lo, hi: hi, vars: v() });
          ms(lo, mid);
          ms(mid + 1, hi);
          // 合并：每次从两个有序部分的开头取较小的一个，放到合并结果的下一个位置
          var i = lo, j = mid + 1, k = lo;
          while (i <= mid && j <= hi) {
            cmp++;
            var o = {}; o[i] = 'cmp'; o[j] = 'cmp';
            for (var d = lo; d < k; d++) o[d] = 'ok';
            var takeLeft = a[i].v <= a[j].v;
            rec.step(12, '合并 [' + lo + ', ' + mid + '] 和 [' + (mid + 1) + ', ' + hi + ']：比较 ' + a[i].v + ' 和 ' + a[j].v + '，取' + (takeLeft ? '左边的 ' + a[i].v + '（相等时取左边，保证稳定）' : '右边的 ' + a[j].v) + '。', { lo: lo, hi: hi, hl: o, ptrs: [{ name: 'i', at: i }, { name: 'j', at: j }], vars: v() });
            if (takeLeft) { i++; }
            else { var el = a.splice(j, 1)[0]; a.splice(i, 0, el); i++; mid++; j++; }
            k++;
          }
          var o2 = {}; for (var d2 = lo; d2 <= hi; d2++) o2[d2] = 'ok';
          rec.step(7, '一边已经取完，另一边剩下的直接接在后面。[' + lo + ', ' + hi + '] 合并完成。', { lo: lo, hi: hi, hl: o2, vars: v() });
        }
        ms(0, a.length - 1);
        rec.step(0, '归并排序完成：比较 ' + cmp + ' 次。无论输入是什么，都是每次对半分，一共 log n 层，每层 O(n)。', { vars: v() });
      }
    }]
  });
})();
