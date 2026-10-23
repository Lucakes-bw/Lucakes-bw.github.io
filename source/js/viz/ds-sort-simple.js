/* 《简单排序》：冒泡、选择、插入排序的柱状图演示 */
(function () {
  'use strict';

  function parse(s) { var a = String(s).split(/[\s,，]+/).filter(Boolean).map(Number); return a.length && a.every(Number.isInteger) ? a : null; }
  function view(m, x) {
    return {
      kind: 'bars', height: 230,
      values: m.a.map(function (e, i) { return { id: e.id, v: e.v, hl: (x.hl || {})[i] || (x.sorted && x.sorted(i) ? 'ok' : null) }; }),
      ptrs: x.ptrs || [], vars: x.vars || {}
    };
  }
  function load(m, list) { m.a = list.map(function (v, i) { return { id: i, v: v }; }); }
  function two(i, j, ci, cj) { var o = {}; o[i] = ci; if (j != null) o[j] = cj; return o; }
  function swap(a, i, j) { var t = a[i]; a[i] = a[j]; a[j] = t; }

  var CODE = [
    'void bubbleSort(int* a, int n)',
    '{',
    '    for (int end = n - 1; end > 0; --end) {',
    '        int swapped = 0;',
    '        for (int i = 0; i < end; ++i)',
    '            if (a[i] > a[i + 1]) { swap(&a[i], &a[i + 1]); swapped = 1; }',
    '        if (!swapped) break;',
    '    }',
    '}',
    '',
    'void selectionSort(int* a, int n)',
    '{',
    '    for (int i = 0; i < n - 1; ++i) {',
    '        int min = i;',
    '        for (int j = i + 1; j < n; ++j)',
    '            if (a[j] < a[min]) min = j;',
    '        swap(&a[i], &a[min]);',
    '    }',
    '}',
    '',
    'void insertionSort(int* a, int n)',
    '{',
    '    for (int i = 1; i < n; ++i) {',
    '        int key = a[i], j = i - 1;',
    '        while (j >= 0 && a[j] > key) {',
    '            a[j + 1] = a[j];',
    '            --j;',
    '        }',
    '        a[j + 1] = key;',
    '    }',
    '}'
  ];

  var SORTS = {
    bubble: function (m, rec) {
      var a = m.a, n = a.length, cmp = 0, sw = 0;
      function v() { return { '比较': cmp, '交换': sw }; }
      for (var end = n - 1; end > 0; --end) {
        var swapped = false;
        for (var i = 0; i < end; ++i) {
          cmp++;
          var big = a[i].v > a[i + 1].v;
          rec.step(6, '比较相邻的 ' + a[i].v + ' 和 ' + a[i + 1].v + (big ? '：前面的大，交换。' : '：顺序正确。'), { hl: two(i, i + 1, 'cmp', 'cmp'), sorted: function (k) { return k > end; }, vars: v() });
          if (big) { swap(a, i, i + 1); sw++; swapped = true; rec.step(6, '交换后，较大的 ' + a[i + 1].v + ' 往后移了一位。', { hl: two(i, i + 1, 'cmp', 'cur'), sorted: function (k) { return k > end; }, vars: v() }); }
        }
        var e = end;
        rec.step(7, '一轮结束，最大的 ' + a[e].v + ' 已经「冒」到了位置 ' + e + '。' + (swapped ? '' : '这一轮没有任何交换，说明已经有序，提前结束。'), { sorted: function (k) { return k >= e; }, vars: v() });
        if (!swapped) break;
      }
      rec.step(9, '冒泡排序完成：比较 ' + cmp + ' 次，交换 ' + sw + ' 次。', { sorted: function () { return true; }, vars: v() });
    },
    selection: function (m, rec) {
      var a = m.a, n = a.length, cmp = 0, sw = 0;
      function v() { return { '比较': cmp, '交换': sw }; }
      for (var i = 0; i < n - 1; ++i) {
        var min = i, ii = i;
        rec.step(14, '在位置 ' + i + ' 到末尾中找最小的，先假设是 ' + a[i].v + '。', { hl: two(i, null, 'cur'), ptrs: [{ name: 'min', at: min }], sorted: function (k) { return k < ii; }, vars: v() });
        for (var j = i + 1; j < n; ++j) {
          cmp++;
          var less = a[j].v < a[min].v;
          if (less) min = j;
          rec.step(16, '比较 ' + a[j].v + (less ? '，更小，min 指向它。' : '，不比 ' + a[min].v + ' 小。'), { hl: two(j, min, 'cmp', 'cur'), ptrs: [{ name: 'min', at: min }], sorted: function (k) { return k < ii; }, vars: v() });
        }
        if (min !== i) { swap(a, i, min); sw++; }
        rec.step(17, '把最小的 ' + a[i].v + ' 换到位置 ' + i + '。' + (min !== i ? '' : '它本来就在这里。'), { hl: two(i, min !== i ? min : null, 'ok', 'cmp'), sorted: function (k) { return k <= ii; }, vars: v() });
      }
      rec.step(19, '选择排序完成：比较 ' + cmp + ' 次，交换 ' + sw + ' 次。无论数据是什么样，比较次数都是 n(n-1)/2。', { sorted: function () { return true; }, vars: v() });
    },
    insertion: function (m, rec) {
      var a = m.a, n = a.length, cmp = 0, mv = 0;
      function v() { return { '比较': cmp, '移动': mv }; }
      for (var i = 1; i < n; ++i) {
        var j = i, ii = i;
        rec.step(24, '取出 key = ' + a[i].v + '，要把它插入到前面已经排好序的 ' + i + ' 个元素中。', { hl: two(i, null, 'new'), sorted: function (k) { return k < ii; }, vars: v() });
        while (j > 0) {
          cmp++;
          if (a[j - 1].v <= a[j].v) { rec.step(25, a[j - 1].v + ' 不比 key 大，找到了位置。', { hl: two(j, j - 1, 'new', 'cmp'), sorted: function (k) { return k <= ii && k !== j; }, vars: v() }); break; }
          swap(a, j - 1, j); mv++;
          var jj = j - 1;
          rec.step(26, '前面的 ' + a[j].v + ' 比 key 大，后移一位，key 继续往前看。', { hl: two(jj, jj + 1, 'new', 'cmp'), sorted: function (k) { return k <= ii && k !== jj; }, vars: v() });
          j--;
        }
        rec.step(29, 'key 放在位置 ' + j + '，前 ' + (i + 1) + ' 个元素有序了。', { sorted: function (k) { return k <= ii; }, vars: v() });
      }
      rec.step(31, '插入排序完成：比较 ' + cmp + ' 次，移动 ' + mv + ' 次。数据越接近有序，需要移动的就越少。', { sorted: function () { return true; }, vars: v() });
    }
  };

  DSViz.register('simple-sorts', {
    title: '冒泡、选择、插入排序',
    height: 230,
    code: CODE,
    init: function () { var m = {}; load(m, [5, 2, 9, 1, 7, 3, 8, 4]); return m; },
    view: view,
    start: function (m, rec) { rec.step(0, '选一种排序开始，可以修改数组，比较三种排序的比较次数。（插入排序中，为了让画面清楚，key 和前面较大的元素逐个交换，效果和「后移、最后放入」相同。）'); },
    actions: [{
      label: '使用这组数据',
      fields: [{ key: 'list', label: '数组', value: '5 2 9 1 7 3 8 4', size: 20 }],
      check: function (m, p) { var a = parse(p.list); if (!a) return '请输入用空格分隔的整数'; if (a.length < 2 || a.length > 12) return '请输入 2 到 12 个数'; return a.some(function (v) { return v < 0 || v > 99; }) ? '每个数在 0 到 99 之间' : null; },
      run: function (m, p, rec) { m.input = parse(p.list); load(m, m.input); rec.step(0, '数组已更新，选一种排序开始。'); }
    }].concat([['冒泡排序', 'bubble'], ['选择排序', 'selection'], ['插入排序', 'insertion']].map(function (x) {
      return {
        label: x[0],
        run: function (m, p, rec) {
          load(m, m.input || [5, 2, 9, 1, 7, 3, 8, 4]);
          rec.step(0, '初始数组。', {});
          SORTS[x[1]](m, rec);
        }
      };
    }))
  });
})();
