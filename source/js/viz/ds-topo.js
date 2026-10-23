/* 《拓扑排序》：Kahn 算法，显示入度、队列和结果 */
(function () {
  'use strict';
  var NAMES = ['C 语言', '数据结构', '离散数学', '算法', '操作系统', '计算机网络', '编译原理'];
  var POS = [[80, 60], [250, 130], [80, 205], [420, 205], [420, 55], [590, 55], [590, 205]];
  var ARCS = [[0, 1], [2, 1], [1, 3], [2, 3], [1, 4], [0, 4], [4, 5], [3, 6], [1, 6]];

  function view(m, x) {
    var arcs = m.cycle ? ARCS.concat([[6, 0]]) : ARCS;
    return {
      kind: 'multi', vars: x.vars || {},
      parts: [{
        kind: 'graph',
        nodes: POS.map(function (p, i) { return { id: i, label: i, x: p[0], y: p[1], hl: (x.hl || {})[i], note: NAMES[i] + (x.indeg ? ' · 入度 ' + x.indeg[i] : '') }; }),
        edges: arcs.map(function (e) { return { a: e[0], b: e[1], directed: true, curve: e[0] === 6 && e[1] === 0 ? -40 : 0, hl: (x.ehl || {})[e[0] + '-' + e[1]] }; })
      }, {
        kind: 'array', x0: 20, cellW: 40, top: 322, noIndex: true, label: '队列（入度为 0、可以开始学的课）',
        cells: (x.queue || []).map(function (v, i) { return { v: v, hl: i === (x.queue.length - 1) && x.qNew ? 'new' : null }; })
      }, {
        kind: 'array', x0: 330, cellW: 40, top: 322, noIndex: true, label: '结果（学习顺序）',
        cells: (x.out || []).map(function (v, i) { return { v: v, hl: i === x.out.length - 1 && x.outNew ? 'new' : null }; })
      }]
    };
  }

  function run(m, rec) {
    var arcs = m.cycle ? ARCS.concat([[6, 0]]) : ARCS;
    var adj = NAMES.map(function () { return []; }), indeg = NAMES.map(function () { return 0; });
    arcs.forEach(function (e) { adj[e[0]].push(e[1]); indeg[e[1]]++; });
    var q = [], out = [], done = {}, removed = {};
    function st(line, msg, cur, extra) {
      extra = extra || {};
      var hl = {};
      Object.keys(done).forEach(function (k) { hl[k] = 'ok'; });
      q.forEach(function (v) { hl[v] = 'cmp'; });
      if (cur != null) hl[cur] = 'cur';
      if (extra.target != null) hl[extra.target] = extra.targetHl || 'new';
      var eh = {};
      Object.keys(removed).forEach(function (k) { eh[k] = 'del'; });
      if (extra.edge) eh[extra.edge] = 'cur';
      rec.step(line, msg, { hl: hl, ehl: eh, indeg: indeg.slice(), queue: q.slice(), out: out.slice(), qNew: extra.qNew, outNew: extra.outNew });
    }
    st(4, '先统计每门课的入度：有几门先修课。箭头 A → B 表示学 B 之前要先学 A。');
    for (var v = 0; v < indeg.length; v++) if (indeg[v] === 0) q.push(v);
    st(6, '入度为 0 的课没有先修要求，可以直接开始：' + (q.map(function (v) { return NAMES[v]; }).join('、') || '一门也没有') + '，放进队列。', null, { qNew: true });
    while (q.length) {
      var u = q.shift();
      out.push(u); done[u] = true;
      st(9, '取出 ' + u + '（' + NAMES[u] + '），加入学习顺序。', u, { outNew: true });
      adj[u].forEach(function (w) {
        indeg[w]--; removed[u + '-' + w] = true;
        if (indeg[w] === 0) {
          q.push(w);
          st(12, '学完 ' + NAMES[u] + '，' + NAMES[w] + ' 的入度减为 0，先修课都学完了，放进队列。', u, { edge: u + '-' + w, target: w, qNew: true });
        } else {
          st(12, '学完 ' + NAMES[u] + '，' + NAMES[w] + ' 的入度减 1，还剩 ' + indeg[w] + ' 门先修课。', u, { edge: u + '-' + w, target: w, targetHl: 'warn' });
        }
      });
    }
    if (out.length === NAMES.length) st(15, '队列为空，7 门课全部排好了：' + out.map(function (v) { return NAMES[v]; }).join(' → ') + '。');
    else st(15, '队列已经空了，但只排出了 ' + out.length + ' 门课，剩下的课入度都不为 0：它们之间互相要求先修，形成了环，无法安排。');
  }

  DSViz.register('topo-kahn', {
    title: 'Kahn 算法：每次选一门先修课都已经学完的课',
    height: 372,
    code: [
      'for (int u = 0; u < g->n; ++u)',
      '    for (int i = 0; i < g->adj[u].size; ++i)',
      '        indeg[g->adj[u].to[i]]++;',
      '/* 统计入度 */',
      'for (int v = 0; v < g->n; ++v)',
      '    if (indeg[v] == 0) queue[tail++] = v;',
      'while (head < tail) {',
      '    int u = queue[head++];',
      '    order[k++] = u;',
      '    for (int i = 0; i < g->adj[u].size; ++i) {',
      '        int v = g->adj[u].to[i];',
      '        if (--indeg[v] == 0) queue[tail++] = v;',
      '    }',
      '}',
      'return k;   /* k < n 说明有环 */'
    ],
    init: function () { return { cycle: false }; },
    view: view,
    start: function (m, rec) { rec.step(0, '7 门课和它们之间的先修关系。点击「开始」求一个合法的学习顺序。'); },
    actions: [{
      label: '开始',
      run: function (m, p, rec) { m.cycle = false; run(m, rec); }
    }, {
      label: '加上 编译原理 → C 语言 再试',
      run: function (m, p, rec) { m.cycle = true; run(m, rec); }
    }]
  });
})();
