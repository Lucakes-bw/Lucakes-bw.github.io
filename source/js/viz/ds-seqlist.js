/* 《顺序表》：插入时元素后移、删除时元素前移、容量不够时扩容 */
(function () {
  'use strict';

  function view(m, x) {
    var cells = [], n = x.len || m.size;          // 插入过程中多显示一个正在使用的位置
    for (var i = 0; i < n; i++) cells.push({ v: m.data[i] == null ? '' : m.data[i], hl: (x.hl || {})[i] });
    return {
      kind: 'array', cells: cells, cap: m.cap, cellW: 50, top: 74, ptrs: x.ptrs || [],
      label: 'size = ' + m.size + '，capacity = ' + m.cap + '（虚线格子是已分配但未使用的空间）',
      vars: x.vars || { size: m.size, capacity: m.cap }
    };
  }

  DSViz.register('seq-ops', {
    title: '顺序表的插入和删除',
    height: 150,
    code: [
      'int insertAt(SeqList* s, size_t pos, int value)',
      '{',
      '    if (pos > s->size) return 0;',
      '    if (s->size == s->capacity && !grow(s)) return 0;',
      '    for (size_t i = s->size; i > pos; --i) {',
      '        s->data[i] = s->data[i - 1];',
      '    }',
      '    s->data[pos] = value;',
      '    s->size++;',
      '    return 1;',
      '}',
      '',
      'int removeAt(SeqList* s, size_t pos, int* out)',
      '{',
      '    if (pos >= s->size) return 0;',
      '    if (out != NULL) *out = s->data[pos];',
      '    for (size_t i = pos; i + 1 < s->size; ++i) {',
      '        s->data[i] = s->data[i + 1];',
      '    }',
      '    s->size--;',
      '    return 1;',
      '}'
    ],
    init: function () { return { data: [10, 20, 30, 40], size: 4, cap: 4 }; },
    view: view,
    start: function (m, rec) { rec.step(0, '容量为 4 的顺序表已经装满了。试试在 pos = 1 插入，会先触发扩容。'); },
    actions: [{
      label: '插入',
      fields: [{ key: 'pos', label: 'pos', value: 1, size: 3 }, { key: 'value', label: 'value', value: 15, size: 4 }],
      check: function (m, p) {
        if (!Number.isInteger(p.pos) || p.pos < 0) return 'pos 要是非负整数';
        if (!Number.isInteger(p.value)) return 'value 要是整数';
        if (m.size >= 12) return '演示区最多放 12 个元素，先删除一些吧';
        return null;
      },
      run: function (m, p, rec) {
        rec.step(3, '检查位置：pos = ' + p.pos + '，size = ' + m.size + '。合法的位置是 0 到 size。');
        if (p.pos > m.size) { rec.step(3, 'pos 超出范围，返回 0。'); return; }
        if (m.size === m.cap) {
          rec.step(4, 'size == capacity，空间已满，调用 grow 扩容。');
          var all = {}; for (var k = 0; k < m.size; k++) all[k] = 'cmp';
          m.cap *= 2;
          rec.step(4, 'grow：申请一块 2 倍大小的新空间（' + m.cap + ' 个），把原来的 ' + m.size + ' 个元素复制过去，再释放旧空间。这一步是 O(n) 的，但容量每次翻倍，所以很少发生。', { hl: all });
        }
        var len = m.size + 1;
        m.data[m.size] = null;
        for (var i = m.size; i > p.pos; --i) {
          var h = {}; h[i - 1] = 'cmp'; h[i] = 'new';
          rec.step(5, 'i = ' + i + ' > pos，要把 data[' + (i - 1) + '] 后移到 data[' + i + ']。', { len: len, hl: h, ptrs: [{ name: 'i', at: i }], vars: { i: i, pos: p.pos } });
          m.data[i] = m.data[i - 1];
          var h2 = {}; h2[i] = 'cur';
          rec.step(6, 'data[' + i + '] = data[' + (i - 1) + ']。必须从后往前移，否则会覆盖还没有移动的元素。', { len: len, hl: h2, ptrs: [{ name: 'i', at: i }], vars: { i: i, pos: p.pos } });
        }
        var o = {}; o[p.pos] = 'new';
        rec.step(5, 'i = pos，循环结束。位置 ' + p.pos + ' 可以放新元素了（原来的值已经复制到了后一格）。', { len: len, hl: o, ptrs: [{ name: 'pos', at: p.pos }], vars: { i: p.pos, pos: p.pos } });
        m.data[p.pos] = p.value;
        m.size++;
        var ok = {}; ok[p.pos] = 'ok';
        rec.step(8, '写入 ' + p.value + '，size 加 1。后面的 ' + (m.size - 1 - p.pos) + ' 个元素各移动了一次：插入的位置越靠前，需要移动的元素越多。', { hl: ok });
      }
    }, {
      label: '删除',
      fields: [{ key: 'pos', label: 'pos', value: 0, size: 3 }],
      check: function (m, p) { return Number.isInteger(p.pos) && p.pos >= 0 ? null : 'pos 要是非负整数'; },
      run: function (m, p, rec) {
        rec.step(15, '检查位置：pos = ' + p.pos + '，size = ' + m.size + '。合法的位置是 0 到 size - 1。');
        if (p.pos >= m.size) { rec.step(15, 'pos 超出范围，返回 0。'); return; }
        var d = {}; d[p.pos] = 'del';
        rec.step(16, '先把要删除的元素 ' + m.data[p.pos] + ' 保存到 *out。', { hl: d, vars: { out: m.data[p.pos], size: m.size } });
        for (var i = p.pos; i + 1 < m.size; ++i) {
          var h = {}; h[i + 1] = 'cmp';
          rec.step(17, 'i = ' + i + '，把 data[' + (i + 1) + '] 前移到 data[' + i + ']。', { hl: h, ptrs: [{ name: 'i', at: i }], vars: { i: i } });
          m.data[i] = m.data[i + 1];
          var h2 = {}; h2[i] = 'cur';
          rec.step(18, 'data[' + i + '] = data[' + (i + 1) + ']。删除时从前往后移。', { hl: h2, ptrs: [{ name: 'i', at: i }], vars: { i: i } });
        }
        m.size--;
        m.data.length = m.size;
        rec.step(20, 'size 减 1。最后一个位置变成了未使用的空间（容量不会自动缩小）。');
      }
    }]
  });
})();
