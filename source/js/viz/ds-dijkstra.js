/* 《最短路径》：堆优化的 Dijkstra，显示 dist、优先队列和最短路径树 */
(function () {
  'use strict';
  var POS = [[70, 140], [210, 50], [230, 170], [400, 50], [560, 130], [400, 225]];
  var E = [[0, 1, 7], [0, 2, 9], [0, 5, 14], [1, 2, 10], [1, 3, 15], [2, 3, 11], [2, 5, 2], [3, 4, 6], [4, 5, 9]];
  function key(u, v) { return u < v ? u + '-' + v : v + '-' + u; }
  function adj() {
    var a = POS.map(function () { return []; });
    E.forEach(function (e) { a[e[0]].push([e[1], e[2]]); a[e[1]].push([e[0], e[2]]); });
    return a;
  }

  // 与 C 代码相同的小顶堆
  function push(h, it) { var i = h.length; h.push(it); while (i > 0 && h[(i - 1) >> 1].d > h[i].d) { var p = (i - 1) >> 1, t = h[p]; h[p] = h[i]; h[i] = t; i = p; } }
  function pop(h) {
    var top = h[0]; h[0] = h[h.length - 1]; h.pop();
    var i = 0;
    for (;;) { var l = 2 * i + 1, r = l + 1, s = i; if (l < h.length && h[l].d < h[s].d) s = l; if (r < h.length && h[r].d < h[s].d) s = r; if (s === i) break; var t = h[i]; h[i] = h[s]; h[s] = t; i = s; }
    return top;
  }

  function view(m, x) {
    var parts = [{
      kind: 'graph',
      nodes: POS.map(function (p, i) { return { id: i, label: i, x: p[0], y: p[1], hl: (x.hl || {})[i], note: x.dist ? 'd=' + (x.dist[i] == null ? '∞' : x.dist[i]) : null }; }),
      edges: E.map(function (e) { return { a: e[0], b: e[1], w: e[2], hl: (x.ehl || {})[key(e[0], e[1])] }; })
    }];
    var q = (x.pq || []).slice().sort(function (a, b) { return a.d - b.d || a.v - b.v; });
    parts.push({ kind: 'array', x0: 20, cellW: 62, top: 318, noIndex: true, label: '优先队列中的 (距离, 顶点)，按距离从小到大画出，灰色是过期的记录', cells: q.map(function (it) { return { v: '(' + it.d + ',' + it.v + ')', hl: x.dist && it.d > x.dist[it.v] ? 'dim' : null }; }) });
    return { kind: 'multi', parts: parts, vars: x.vars || {} };
  }

  DSViz.register('dijkstra', {
    title: 'Dijkstra：每次确定一个距离最小的顶点',
    height: 370,
    code: [
      'dist[s] = 0;',
      'pqPush(&q, (Item){0, s});',
      'while (q.size > 0) {',
      '    Item it = pqPop(&q);',
      '    int u = it.v;',
      '    if (it.d > dist[u]) continue;',
      '    for (int i = 0; i < g->adj[u].size; ++i) {',
      '        Edge e = g->adj[u].e[i];',
      '        if (dist[u] + e.w < dist[e.to]) {',
      '            dist[e.to] = dist[u] + e.w;',
      '            prev[e.to] = u;',
      '            pqPush(&q, (Item){dist[e.to], e.to});',
      '        }',
      '    }',
      '}'
    ],
    init: function () { return {}; },
    view: view,
    start: function (m, rec) { rec.step(0, '求顶点 0 到其他所有顶点的最短距离。边上的数字是长度。点击「开始」。'); },
    actions: [{
      label: '开始',
      run: function (m, p, rec) {
        var a = adj(), dist = {}, prev = {}, done = {}, pq = [];
        function st(line, msg, cur, extra) {
          extra = extra || {};
          var hl = {}, eh = {};
          Object.keys(dist).forEach(function (k) { hl[k] = done[k] ? 'ok' : 'cmp'; });
          Object.keys(prev).forEach(function (k) { eh[key(+k, prev[k])] = 'ok'; });
          if (cur != null) hl[cur] = 'cur';
          if (extra.edge) eh[extra.edge] = extra.edgeHl || 'cur';
          if (extra.target != null && !done[extra.target]) hl[extra.target] = extra.targetHl || 'new';
          var dd = {}; Object.keys(dist).forEach(function (k) { dd[k] = dist[k]; });
          rec.step(line, msg, { hl: hl, ehl: eh, dist: dd, pq: pq.slice(), vars: { '已确定': Object.keys(done).join(' ') || '（无）' } });
        }
        dist[0] = 0; push(pq, { d: 0, v: 0 });
        st(2, '起点 0 的距离是 0，其他顶点暂时是 ∞。把 (0, 0) 放进优先队列。', null);
        while (pq.length) {
          var it = pop(pq), u = it.v;
          if (it.d > dist[u]) { st(6, '取出 (' + it.d + ', ' + u + ')，但 dist[' + u + '] 已经是更小的 ' + dist[u] + '，这是一条过期的记录，跳过。', null); continue; }
          done[u] = true;
          st(4, '取出距离最小的 (' + it.d + ', ' + u + ')。剩下的顶点都比它远，不可能再找到更短的路到 ' + u + '，dist[' + u + '] = ' + dist[u] + ' 确定了。', u);
          a[u].forEach(function (e) {
            var v = e[0], w = e[1];
            if (done[v]) return;
            var nd = dist[u] + w;
            if (dist[v] == null || nd < dist[v]) {
              var old = dist[v] == null ? '∞' : dist[v];
              dist[v] = nd; prev[v] = u; push(pq, { d: nd, v: v });
              st(12, '松弛边 ' + u + '–' + v + '：' + dist[u] + ' + ' + w + ' = ' + nd + ' < ' + old + '，更新 dist[' + v + '] = ' + nd + '，记录前驱是 ' + u + '，放进队列。', u, { edge: key(u, v), target: v });
            } else {
              st(9, '松弛边 ' + u + '–' + v + '：' + dist[u] + ' + ' + w + ' = ' + nd + '，不比 dist[' + v + '] = ' + dist[v] + ' 小，不更新。', u, { edge: key(u, v), edgeHl: 'del', target: v, targetHl: 'cmp' });
            }
          });
        }
        st(3, '队列为空，完成。绿色的边是每个顶点的「前驱」连成的最短路径树，比如到 4 的最短路是 0 → 2 → 5 → 4，长度 20。', null);
      }
    }]
  });
})();
