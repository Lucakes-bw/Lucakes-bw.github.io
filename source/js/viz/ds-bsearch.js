/* 《二分查找》：lowerBound 和 upperBound 在有重复元素的数组中查找边界 */
(function () {
  'use strict';
  var A = [1, 3, 3, 3, 5, 8, 8, 13];

  function view(m, x) {
    var lo = x.lo, hi = x.hi;
    return {
      kind: 'array', cellW: 56, x0: 120, top: 80, cap: A.length + 1,
      cells: A.map(function (v, i) { return { v: v, hl: (x.hl || {})[i] || (lo != null && (i < lo || i >= hi) ? 'dim' : null) }; }),
      ptrs: x.ptrs || [], label: '左闭右开区间 [lo, hi)：亮着的部分是答案还可能在的范围（下标 8 表示「数组末尾之后」）',
      vars: x.vars || {}
    };
  }

  function run(m, key, upper, rec) {
    var lo = 0, hi = A.length, round = 0;
    var L = upper ? { init: 14, cmp: 17, loop: 15, ret: 20 } : { init: 3, cmp: 6, loop: 4, ret: 9 };
    function ptrs(mid) { var p = [{ name: 'lo', at: lo }, { name: 'hi', at: hi, hl: 'warn' }]; if (mid != null) p.push({ name: 'mid', at: mid }); return p; }
    rec.step(L.init, '在 [0, ' + A.length + ') 中查找第一个' + (upper ? '大于 ' : '大于等于 ') + key + ' 的位置。hi 取 n 而不是 n - 1：如果所有元素都不满足，答案就是 n。', { lo: lo, hi: hi, ptrs: ptrs(), vars: { lo: lo, hi: hi } });
    while (lo < hi) {
      round++;
      var mid = lo + Math.floor((hi - lo) / 2);
      var small = upper ? A[mid] <= key : A[mid] < key;
      var o = {}; o[mid] = 'cmp';
      rec.step(L.cmp, '第 ' + round + ' 轮：mid = ' + mid + '，a[mid] = ' + A[mid] + (small ? (upper ? ' <= ' : ' < ') + key + '，mid 和它左边的都不是答案，lo = mid + 1。' : (upper ? ' > ' : ' >= ') + key + '，mid 可能就是答案，保留它：hi = mid。'), { lo: lo, hi: hi, hl: o, ptrs: ptrs(mid), vars: { lo: lo, hi: hi, mid: mid } });
      if (small) lo = mid + 1; else hi = mid;
      rec.step(L.loop, '区间缩小为 [' + lo + ', ' + hi + ')，还剩 ' + (hi - lo) + ' 个位置。', { lo: lo, hi: hi, ptrs: ptrs(), vars: { lo: lo, hi: hi } });
    }
    var ok = {}; if (lo < A.length) ok[lo] = 'ok';
    rec.step(L.ret, 'lo == hi，区间为空，答案是 ' + lo + (lo < A.length ? '（a[' + lo + '] = ' + A[lo] + '）' : '（所有元素都不满足，返回 n）') + '。只比较了 ' + round + ' 次。', { hl: ok, ptrs: [{ name: 'lo', at: lo }], vars: { '结果': lo, '比较次数': round } });
  }

  DSViz.register('lower-bound', {
    title: 'lowerBound / upperBound：在重复的元素中找边界',
    height: 170,
    code: [
      'int lowerBound(const int* a, int n, int key)',
      '{',
      '    int lo = 0, hi = n;',
      '    while (lo < hi) {',
      '        int mid = lo + (hi - lo) / 2;',
      '        if (a[mid] < key) lo = mid + 1;',
      '        else hi = mid;',
      '    }',
      '    return lo;',
      '}',
      '',
      'int upperBound(const int* a, int n, int key)',
      '{',
      '    int lo = 0, hi = n;',
      '    while (lo < hi) {',
      '        int mid = lo + (hi - lo) / 2;',
      '        if (a[mid] <= key) lo = mid + 1;',
      '        else hi = mid;',
      '    }',
      '    return lo;',
      '}'
    ],
    init: function () { return {}; },
    view: view,
    start: function (m, rec) { rec.step(0, '数组中 3 出现了 3 次。lowerBound(3) 找第一个 3，upperBound(3) 找最后一个 3 的下一个位置，两者相减就是 3 的个数。试试查找 3、4、0、100。'); },
    actions: [['lowerBound', false], ['upperBound', true]].map(function (x) {
      return {
        label: x[0],
        fields: [{ key: 'key', label: 'key', value: 3, size: 4 }],
        check: function (m, p) { return Number.isInteger(p.key) ? null : '请输入整数'; },
        run: function (m, p, rec) { run(m, p.key, x[1], rec); }
      };
    })
  });
})();
