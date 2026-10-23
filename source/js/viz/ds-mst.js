/* 《最小生成树与并查集》：并查集的合并与路径压缩、Kruskal 算法 */
(function () {
  'use strict';

  // ---------- 并查集的森林 ----------
  function forestView(parent, x, opts) {
    opts = opts || {};
    var n = parent.length, kids = parent.map(function () { return []; }), roots = [];
    for (var i = 0; i < n; i++) { if (parent[i] === i) roots.push(i); else kids[parent[i]].push(i); }
    var pos = {}, col = 0;
    function place(v, d) {
      if (!kids[v].length) { pos[v] = { x: col++, d: d }; return; }
      kids[v].forEach(function (c) { place(c, d + 1); });
      var xs = kids[v].map(function (c) { return pos[c].x; });
      pos[v] = { x: (Math.min.apply(null, xs) + Math.max.apply(null, xs)) / 2, d: d };
    }
    roots.forEach(function (r) { place(r, 0); col += 0.5; });
    var gap = Math.min(70, 640 / Math.max(col, 1)), x0 = (720 - (col - 1.5) * gap) / 2;
    var nodes = [], edges = [];
    for (var v = 0; v < n; v++) {
      nodes.push({ id: v, label: v, x: x0 + pos[v].x * gap, y: (opts.top || 50) + pos[v].d * 62, hl: (x.hl || {})[v], note: parent[v] === v ? '根' : null });
      if (parent[v] !== v) edges.push({ a: v, b: parent[v], directed: true, hl: (x.ehl || {})[v] });
    }
    return { kind: 'graph', nodes: nodes, edges: edges, vars: x.vars || {} };
  }

  function findSteps(p, x, rec, line, label) {
    var path = [x], r = x;
    while (p[r] !== r) { r = p[r]; path.push(r); }
    var hl = {}; path.forEach(function (v) { hl[v] = 'cmp'; }); hl[r] = 'ok';
    rec.step(line, label + '：从 ' + x + ' 沿着父指针向上走 ' + (path.length - 1) + ' 步，找到根 ' + r + '。', { hl: hl });
    var changed = [];
    for (var i = 0; i < path.length - 1; i++) {
      var v = path[i];
      if (p[v] !== r) { p[v] = r; changed.push(v); }
    }
    if (changed.length) {
      var h2 = {}; changed.forEach(function (v) { h2[v] = 'new'; }); h2[r] = 'ok';
      rec.step(line, '路径压缩：把沿途的 ' + changed.join('、') + ' 直接挂到根 ' + r + ' 上。下次再查找它们只需要一步。', { hl: h2 });
    }
    return r;
  }

  DSViz.register('union-find', {
    title: '并查集：箭头指向父节点，每棵树是一个集合',
    height: 300,
    code: [
      'int find(UnionFind* uf, int x)',
      '{',
      '    int root = x;',
      '    while (uf->parent[root] != root) root = uf->parent[root];',
      '    while (uf->parent[x] != root) {',
      '        int next = uf->parent[x];',
      '        uf->parent[x] = root;',
      '        x = next;',
      '    }',
      '    return root;',
      '}',
      '',
      'int unite(UnionFind* uf, int a, int b)',
      '{',
      '    int ra = find(uf, a), rb = find(uf, b);',
      '    if (ra == rb) return 0;',
      '    if (uf->rank[ra] < uf->rank[rb]) { int t = ra; ra = rb; rb = t; }',
      '    uf->parent[rb] = ra;',
      '    if (uf->rank[ra] == uf->rank[rb]) uf->rank[ra]++;',
      '    uf->count--;',
      '    return 1;',
      '}'
    ],
    init: function () { return { p: [0, 1, 2, 3, 4, 5, 6, 7], rank: [0, 0, 0, 0, 0, 0, 0, 0] }; },
    view: function (m, x) { var v = forestView(m.p, x); v.vars = x.vars || { '集合个数': m.p.filter(function (q, i) { return q === i; }).length }; return v; },
    start: function (m, rec) { rec.step(0, '8 个元素，一开始各自是一个集合（每个都是根）。依次试试合并 0 和 1、2 和 3、0 和 2、4 和 5、5 和 3，然后查找 1。'); },
    actions: [{
      label: '合并',
      fields: [{ key: 'a', label: 'a', value: 0, size: 2 }, { key: 'b', label: 'b', value: 1, size: 2 }],
      check: function (m, p) { return [p.a, p.b].every(function (v) { return Number.isInteger(v) && v >= 0 && v < 8; }) ? null : 'a 和 b 的范围是 0 到 7'; },
      run: function (m, p, rec) {
        var ra = findSteps(m.p, p.a, rec, 15, '查找 ' + p.a + ' 的根');
        var rb = findSteps(m.p, p.b, rec, 15, '查找 ' + p.b + ' 的根');
        if (ra === rb) { var h = {}; h[ra] = 'warn'; rec.step(16, p.a + ' 和 ' + p.b + ' 的根相同，已经在同一个集合中了，不需要合并。', { hl: h }); return; }
        if (m.rank[ra] < m.rank[rb]) { var t = ra; ra = rb; rb = t; }
        m.p[rb] = ra;
        var note = m.rank[ra] === m.rank[rb] ? '两棵树一样高，合并后高度加 1。' : '矮的树挂到高的树下面，总高度不变。';
        if (m.rank[ra] === m.rank[rb]) m.rank[ra]++;
        var hl = {}; hl[ra] = 'ok'; hl[rb] = 'new';
        var eh = {}; eh[rb] = 'cur';
        rec.step(18, '把根 ' + rb + ' 挂到根 ' + ra + ' 下面，两个集合合并成一个。' + note, { hl: hl, ehl: eh });
      }
    }, {
      label: '查找',
      fields: [{ key: 'x', label: 'x', value: 1, size: 2 }],
      check: function (m, p) { return Number.isInteger(p.x) && p.x >= 0 && p.x < 8 ? null : 'x 的范围是 0 到 7'; },
      run: function (m, p, rec) { var r = findSteps(m.p, p.x, rec, 4, '查找 ' + p.x); rec.step(10, p.x + ' 所在集合的代表（根）是 ' + r + '。判断两个元素是否在同一个集合，只要比较它们的根。'); }
    }, {
      label: '恢复',
      run: function (m, p, rec) { m.p = [0, 1, 2, 3, 4, 5, 6, 7]; m.rank = [0, 0, 0, 0, 0, 0, 0, 0]; rec.step(0, '已恢复。'); }
    }]
  });

  // ---------- Kruskal ----------
  var POS = [[70, 140], [210, 50], [230, 170], [400, 50], [560, 130], [400, 225]];
  var E = [[0, 1, 7], [0, 2, 9], [0, 5, 14], [1, 2, 10], [1, 3, 15], [2, 3, 11], [2, 5, 2], [3, 4, 6], [4, 5, 9]];
  function key(u, v) { return u < v ? u + '-' + v : v + '-' + u; }

  DSViz.register('kruskal', {
    title: 'Kruskal：从小到大选边，跳过会形成环的边',
    height: 340,
    code: [
      'qsort(edges, m, sizeof(WEdge), byWeight);',
      'UnionFind uf = ufCreate(n);',
      'for (int i = 0; i < m && k < n - 1; ++i) {',
      '    if (unite(&uf, edges[i].u, edges[i].v)) {',
      '        chosen[k++] = edges[i];',
      '        total += edges[i].w;',
      '    }',
      '}'
    ],
    init: function () { return {}; },
    view: function (m, x) {
      var sorted = x.sorted || [];
      return {
        kind: 'multi', vars: x.vars || {},
        parts: [{
          kind: 'graph',
          nodes: POS.map(function (p, i) { return { id: i, label: i, x: p[0], y: p[1], hl: (x.hl || {})[i], note: x.root ? '根 ' + x.root[i] : null }; }),
          edges: E.map(function (e) { return { a: e[0], b: e[1], w: e[2], hl: (x.ehl || {})[key(e[0], e[1])] }; })
        }, {
          kind: 'array', x0: 20, cellW: 74, top: 308, noIndex: true, label: '按权重排好序的边',
          cells: sorted.map(function (e, i) { return { v: e[0] + '-' + e[1] + ':' + e[2], hl: (x.listHl || {})[i] }; })
        }]
      };
    },
    start: function (m, rec) { rec.step(0, '求连接所有顶点、总权重最小的一组边。点击「开始」。'); },
    actions: [{
      label: '开始',
      run: function (m, p, rec) {
        var sorted = E.slice().sort(function (a, b) { return a[2] - b[2]; });
        var parent = [0, 1, 2, 3, 4, 5], rank = [0, 0, 0, 0, 0, 0], eh = {}, listHl = {}, total = 0, k = 0;
        function root(v) { while (parent[v] !== v) v = parent[v]; return v; }
        function roots() { return parent.map(function (q, i) { return root(i); }); }
        rec.step(1, '先把 9 条边按权重从小到大排序。一开始每个顶点自己是一棵树（并查集中每个顶点是一个集合）。', { sorted: sorted, root: roots() });
        for (var i = 0; i < sorted.length && k < 5; i++) {
          var e = sorted[i], ru = root(e[0]), rv = root(e[1]);
          var lh = Object.assign({}, listHl); lh[i] = 'cur';
          var hl = {}; hl[e[0]] = 'cmp'; hl[e[1]] = 'cmp';
          var cur = Object.assign({}, eh); cur[key(e[0], e[1])] = 'cur';
          rec.step(4, '看边 ' + e[0] + '-' + e[1] + '（权重 ' + e[2] + '）：' + e[0] + ' 的根是 ' + ru + '，' + e[1] + ' 的根是 ' + rv + '。', { sorted: sorted, listHl: lh, hl: hl, ehl: cur, root: roots() });
          if (ru !== rv) {
            if (rank[ru] < rank[rv]) { var t = ru; ru = rv; rv = t; }
            parent[rv] = ru; if (rank[ru] === rank[rv]) rank[ru]++;
            eh[key(e[0], e[1])] = 'ok'; listHl[i] = 'ok'; total += e[2]; k++;
            rec.step(6, '根不同，两端在不同的树中，选择这条边，把两棵树合并。已选 ' + k + ' 条，总权重 ' + total + '。', { sorted: sorted, listHl: listHl, ehl: eh, root: roots(), vars: { '已选边数': k, '总权重': total } });
          } else {
            eh[key(e[0], e[1])] = 'del'; listHl[i] = 'dim';
            rec.step(4, '根相同，两端已经连通了，再加这条边就会形成环，跳过。', { sorted: sorted, listHl: listHl, ehl: eh, root: roots(), vars: { '已选边数': k, '总权重': total } });
            delete eh[key(e[0], e[1])];
          }
        }
        rec.step(3, '已经选了 n - 1 = 5 条边，所有顶点都连通了，结束。最小生成树的总权重是 ' + total + '。', { sorted: sorted, listHl: listHl, ehl: eh, root: roots(), vars: { '已选边数': k, '总权重': total } });
      }
    }]
  });
})();
