/* 《图的表示与遍历》：DFS（显示递归调用栈）和 BFS（显示队列和距离） */
(function () {
  'use strict';
  var POS = [[80, 120], [200, 55], [200, 185], [330, 35], [330, 120], [330, 205], [460, 80], [570, 150]];
  var EDGES = [[0, 1], [0, 2], [1, 3], [1, 4], [2, 5], [3, 6], [4, 6], [5, 7], [6, 7]];
  function adj() {                                  // 和 C 代码相同的插入顺序
    var a = POS.map(function () { return []; });
    EDGES.forEach(function (e) { a[e[0]].push(e[1]); a[e[1]].push(e[0]); });
    return a;
  }
  function key(u, v) { return u < v ? u + '-' + v : v + '-' + u; }
  function graphPart(x) {
    return {
      kind: 'graph',
      nodes: POS.map(function (p, i) { return { id: i, label: i, x: p[0], y: p[1], hl: (x.hl || {})[i], note: (x.note || {})[i] }; }),
      edges: EDGES.map(function (e) { return { a: e[0], b: e[1], hl: (x.ehl || {})[key(e[0], e[1])] }; })
    };
  }
  function row(vals, top, label, hl) {
    return { kind: 'array', x0: 20, cellW: 40, top: top, label: label, noIndex: true, cells: vals.map(function (v, i) { return { v: v, hl: (hl || {})[i] }; }) };
  }

  DSViz.register('graph-dfs', {
    title: '深度优先搜索（DFS）：一条路走到底，再回头',
    height: 340,
    code: [
      'void dfs(const Graph* g, int v, int* visited)',
      '{',
      '    visited[v] = 1;',
      '    printf("%d ", v);',
      '    for (int i = 0; i < g->adj[v].size; ++i) {',
      '        int w = g->adj[v].to[i];',
      '        if (!visited[w]) dfs(g, w, visited);',
      '    }',
      '}'
    ],
    init: function () { return {}; },
    view: function (m, x) {
      var st = (x.stack || []).map(function (v, i, a) { return { v: 'dfs(' + v + ')', hl: i === a.length - 1 ? 'cur' : null }; });
      return {
        kind: 'multi', vars: x.vars || {},
        parts: [graphPart(x),
          { kind: 'array', vertical: true, x0: 620, top: 40, cap: 5, cellW: 80, noIndex: true, label: '调用栈', cells: st.slice(-5), ptrs: [] },
          row(x.out || [], 290, '访问顺序', x.outHl)]
      };
    },
    start: function (m, rec) { rec.step(0, '从顶点 0 出发。每到一个顶点，就按邻接表的顺序找第一个没有访问过的邻居，继续深入；没有路可走时，返回上一层。'); },
    actions: [{
      label: '开始 DFS',
      run: function (m, p, rec) {
        var a = adj(), visited = {}, stack = [], out = [], tree = {};
        function st(line, msg, cur, extra) {
          extra = extra || {};
          var hl = {};
          Object.keys(visited).forEach(function (k) { hl[k] = 'ok'; });
          stack.forEach(function (v) { hl[v] = 'cmp'; });
          if (cur != null) hl[cur] = 'cur';
          if (extra.look != null && !visited[extra.look]) hl[extra.look] = 'new';
          var eh = Object.assign({}, tree);
          if (extra.edge) eh[extra.edge] = extra.edgeHl || 'cur';
          var oh = {}; if (extra.outNew) oh[out.length - 1] = 'new';
          rec.step(line, msg, { hl: hl, ehl: eh, stack: stack.slice(), out: out.slice(), outHl: oh, vars: { '递归深度': stack.length } });
        }
        function dfs(v) {
          stack.push(v);
          visited[v] = true; out.push(v);
          st(4, '进入 dfs(' + v + ')，标记为已访问，输出 ' + v + '。', v, { outNew: true });
          a[v].forEach(function (w) {
            if (visited[w]) {
              st(7, v + ' 的邻居 ' + w + ' 已经访问过了，跳过。', v, { edge: key(v, w), edgeHl: 'del' });
            } else {
              st(7, v + ' 的邻居 ' + w + ' 还没有访问，沿着这条边走过去。', v, { look: w, edge: key(v, w) });
              tree[key(v, w)] = 'ok';
              dfs(w);
              st(5, '从 ' + w + ' 返回到 ' + v + '，继续看 ' + v + ' 的下一个邻居。', v);
            }
          });
          stack.pop();
          st(9, v + ' 的邻居都处理完了，dfs(' + v + ') 返回。', stack.length ? stack[stack.length - 1] : null);
        }
        dfs(0);
        st(0, 'DFS 完成：' + out.join(' ') + '。绿色的边是 DFS 实际走过的边，它们组成了一棵「DFS 树」。', null);
      }
    }]
  });

  DSViz.register('graph-bfs', {
    title: '广度优先搜索（BFS）：由近及远，一圈一圈地扩展',
    height: 330,
    code: [
      'dist[s] = 0;',
      'queue[tail++] = s;',
      'while (head < tail) {',
      '    int v = queue[head++];',
      '    for (int i = 0; i < g->adj[v].size; ++i) {',
      '        int w = g->adj[v].to[i];',
      '        if (dist[w] == -1) {',
      '            dist[w] = dist[v] + 1;',
      '            queue[tail++] = w;',
      '        }',
      '    }',
      '}'
    ],
    init: function () { return {}; },
    view: function (m, x) {
      return { kind: 'multi', vars: x.vars || {}, parts: [graphPart(x), row(x.queue || [], 272, '队列（左边是队头）', x.qHl)] };
    },
    start: function (m, rec) { rec.step(0, '从顶点 0 出发。顶点下方显示 dist：到 0 最少要经过几条边。'); },
    actions: [{
      label: '开始 BFS',
      run: function (m, p, rec) {
        var a = adj(), dist = {}, q = [], done = {}, tree = {}, order = [];
        function st(line, msg, cur, extra) {
          extra = extra || {};
          var hl = {}, note = {};
          Object.keys(dist).forEach(function (k) { hl[k] = done[k] ? 'ok' : 'cmp'; note[k] = 'dist=' + dist[k]; });
          if (cur != null) hl[cur] = 'cur';
          var eh = Object.assign({}, tree);
          if (extra.edge) eh[extra.edge] = extra.edgeHl || 'cur';
          var qh = {}; if (extra.qNew) qh[q.length - 1] = 'new';
          rec.step(line, msg, { hl: hl, note: note, ehl: eh, queue: q.slice(), qHl: qh, vars: { '访问顺序': order.join(' ') || '（无）' } });
        }
        dist[0] = 0; q.push(0);
        st(2, '起点 0 的距离是 0，入队。', null, { qNew: true });
        while (q.length) {
          var v = q.shift();
          done[v] = true; order.push(v);
          st(4, '队头 ' + v + ' 出队（距离 ' + dist[v] + '），检查它的邻居。', v);
          a[v].forEach(function (w) {
            if (dist[w] != null) {
              st(7, '邻居 ' + w + ' 已经被发现过了（dist = ' + dist[w] + '），跳过。', v, { edge: key(v, w), edgeHl: 'del' });
            } else {
              dist[w] = dist[v] + 1; q.push(w); tree[key(v, w)] = 'ok';
              st(9, '第一次到达 ' + w + '：dist[' + w + '] = dist[' + v + '] + 1 = ' + dist[w] + '，入队。', v, { edge: key(v, w), qNew: true });
            }
          });
        }
        st(3, '队列为空，BFS 完成。距离为 1 的顶点全部在距离为 2 的之前出队，所以每个顶点第一次被发现时，得到的就是最少的边数。', null);
      }
    }]
  });
})();
