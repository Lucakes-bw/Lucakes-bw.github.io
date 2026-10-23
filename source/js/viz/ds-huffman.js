/* 《哈夫曼树与哈夫曼编码》：反复合并权重最小的两棵树；和正文 C 代码使用相同的小顶堆，得到相同的编码 */
(function () {
  'use strict';

  function show(c) { return c === ' ' ? '␣' : c; }

  // 与 C 代码相同的小顶堆（按 weight 比较）
  function hpush(h, n, nodes) {
    var i = h.length; h.push(n);
    while (i > 0 && nodes[h[(i - 1) >> 1]].w > nodes[h[i]].w) { var p = (i - 1) >> 1, t = h[p]; h[p] = h[i]; h[i] = t; i = p; }
  }
  function hpop(h, nodes) {
    var top = h[0]; h[0] = h[h.length - 1]; h.pop();
    var i = 0;
    for (;;) {
      var l = 2 * i + 1, r = l + 1, s = i;
      if (l < h.length && nodes[h[l]].w < nodes[h[s]].w) s = l;
      if (r < h.length && nodes[h[r]].w < nodes[h[s]].w) s = r;
      if (s === i) break;
      var t = h[i]; h[i] = h[s]; h[s] = t; i = s;
    }
    return top;
  }

  function layout(m) {
    // 森林中的每棵树从左到右排列：按权重从小到大，权重相同按创建顺序
    var roots = m.roots.slice().sort(function (a, b) { return m.nodes[a].w - m.nodes[b].w || a - b; });
    var pos = {}, col = 0;
    function place(id, d) {
      var n = m.nodes[id];
      if (n.l == null) { pos[id] = { x: col++, d: d }; return; }
      place(n.l, d + 1); place(n.r, d + 1);
      pos[id] = { x: (pos[n.l].x + pos[n.r].x) / 2, d: d };
    }
    roots.forEach(function (r) { place(r, 0); col += 0.4; });
    return { pos: pos, cols: col };
  }
  function view(m, x) {
    var L = layout(m), gap = Math.min(80, 660 / Math.max(L.cols, 1)), nodes = [], edges = [];
    var x0 = (720 - (L.cols - 1.4) * gap) / 2;
    var maxD = 0; Object.keys(L.pos).forEach(function (k) { if (L.pos[k].d > maxD) maxD = L.pos[k].d; });
    var dy = Math.min(54, 200 / Math.max(maxD, 1));
    Object.keys(L.pos).forEach(function (k) {
      var n = m.nodes[k], p = L.pos[k];
      nodes.push({ id: +k, label: n.w, x: x0 + p.x * gap, y: 40 + p.d * dy, hl: (x.hl || {})[k], note: n.l == null ? '\'' + show(n.ch) + '\'' + (x.codes ? ' ' + x.codes[n.ch] : '') : null });
      if (n.l != null) {
        edges.push({ a: +k, b: n.l, w: '0', hl: (x.ehl || {})[k] });
        edges.push({ a: +k, b: n.r, w: '1', hl: (x.ehl || {})[k] });
      }
    });
    return { kind: 'graph', nodes: nodes, edges: edges, vars: x.vars || {} };
  }

  function start(m, text) {
    m.nodes = {}; m.roots = []; m.text = text;
    var freq = {}, id = 0;
    for (var i = 0; i < text.length; i++) freq[text[i]] = (freq[text[i]] || 0) + 1;
    m.heap = [];
    Object.keys(freq).sort(function (a, b) { return a.charCodeAt(0) - b.charCodeAt(0); }).forEach(function (c) {
      var nid = ++id;
      m.nodes[nid] = { ch: c, w: freq[c], l: null, r: null };
      m.roots.push(nid);
      hpush(m.heap, nid, m.nodes);
    });
    m.nextId = id + 1;
    return freq;
  }

  DSViz.register('huffman-build', {
    title: '构建哈夫曼树：每次合并权重最小的两棵树',
    height: 300,
    code: [
      'while (heap.size > 1) {',
      '    HNode* a = hpop(&heap);',
      '    HNode* b = hpop(&heap);',
      '    hpush(&heap, newNode(0, a->weight + b->weight, a, b));',
      '}',
      'return hpop(&heap);'
    ],
    init: function () { var m = {}; start(m, 'abracadabra'); return m; },
    view: view,
    start: function (m, rec) { rec.step(0, '"abracadabra" 中每个字符是一个节点，圆圈中的数字是出现的次数（权重），下方是字符。点击「构建」。'); },
    actions: [{
      label: '构建',
      fields: [{ key: 'text', label: '文本', value: 'abracadabra', size: 18 }],
      check: function (m, p) {
        var t = String(p.text);
        if (t.length < 2) return '至少输入 2 个字符';
        if (t.length > 40) return '最多 40 个字符';
        var kinds = {}; for (var i = 0; i < t.length; i++) kinds[t[i]] = 1;
        var k = Object.keys(kinds).length;
        if (k < 2) return '至少要有两种不同的字符';
        return k > 9 ? '演示最多 9 种不同的字符' : null;
      },
      run: function (m, p, rec) {
        var freq = start(m, String(p.text));
        rec.step(0, '统计每个字符出现的次数，每个字符是一棵只有一个节点的树，放进按权重排序的小顶堆。', { vars: freq });
        var round = 0;
        while (m.heap.length > 1) {
          round++;
          var a = hpop(m.heap, m.nodes), b = hpop(m.heap, m.nodes);
          var hl = {}; hl[a] = 'cmp'; hl[b] = 'cmp';
          rec.step(3, '第 ' + round + ' 次合并：取出权重最小的两棵树（' + m.nodes[a].w + ' 和 ' + m.nodes[b].w + '）。', { hl: hl });
          var nid = m.nextId++;
          m.nodes[nid] = { ch: null, w: m.nodes[a].w + m.nodes[b].w, l: a, r: b };
          m.roots = m.roots.filter(function (r) { return r !== a && r !== b; });
          m.roots.push(nid);
          hpush(m.heap, nid, m.nodes);
          var h2 = {}; h2[nid] = 'new';
          var eh = {}; eh[nid] = 'cur';
          rec.step(4, '把它们作为左右孩子，合并成一棵新树，根的权重是 ' + m.nodes[nid].w + '，放回堆中。森林中还剩 ' + m.roots.length + ' 棵树。', { hl: h2, ehl: eh });
        }
        var codes = {};
        (function walk(id, s) {
          var n = m.nodes[id];
          if (n.l == null) { codes[n.ch] = s || '0'; return; }
          walk(n.l, s + '0'); walk(n.r, s + '1');
        })(m.roots[0], '');
        var root = m.roots[0], bits = 0, chips = {};
        Object.keys(freq).forEach(function (c) { bits += freq[c] * codes[c].length; chips['\'' + show(c) + '\''] = codes[c]; });
        var ok = {}; ok[root] = 'ok';
        rec.step(6, '只剩一棵树，构建完成。从根走到每个叶子，左边记 0，右边记 1，得到每个字符的编码（叶子下方）。出现次数多的字符离根近，编码短。', { hl: ok, codes: codes, vars: chips });
        rec.step(6, '编码后一共 ' + bits + ' 位，原来每个字符 8 位，共 ' + m.text.length * 8 + ' 位。没有任何一个编码是另一个编码的前缀，所以解码时不需要分隔符。', { codes: codes, vars: chips });
      }
    }]
  });
})();
