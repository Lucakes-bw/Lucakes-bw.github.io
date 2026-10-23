/* 《双向链表与循环链表》：双向链表的插入、删除，用循环链表解约瑟夫问题 */
(function () {
  'use strict';

  // ---------- 双向链表 ----------
  function makeList(values) {
    var m = { nodes: { 0: { val: 'H', prev: null, next: null } }, nextId: 1, order: [], float: {} };
    var prev = 0;
    values.forEach(function (v) {
      var id = m.nextId++;
      m.nodes[id] = { val: v, prev: prev, next: null };
      m.nodes[prev].next = id;
      prev = id;
    });
    relayout(m);
    return m;
  }
  function relayout(m) {
    m.order = [];
    for (var p = 0; p != null; p = m.nodes[p].next) m.order.push(p);
    m.float = {};
  }
  function values(m) {
    var out = [];
    for (var p = m.nodes[0].next; p != null; p = m.nodes[p].next) out.push(m.nodes[p].val);
    return out;
  }
  function find(m, v) {
    for (var p = m.nodes[0].next; p != null; p = m.nodes[p].next) if (m.nodes[p].val === v) return p;
    return null;
  }
  function nameOf(m, id) { return id == null ? 'NULL' : (id === 0 ? '头结点' : '节点 ' + m.nodes[id].val); }
  function view(m, x) {
    var nodes = [], links = [];
    m.order.forEach(function (id, i) {
      if (!m.nodes[id]) return;
      nodes.push({ id: id, val: m.nodes[id].val, col: i, hl: (x.hl || {})[id], sub: id === 0 ? '头结点' : null });
    });
    Object.keys(m.float).forEach(function (id) {
      if (!m.nodes[id]) return;
      nodes.push({ id: +id, val: m.nodes[id].val, col: m.float[id].col, row: m.float[id].row, hl: (x.hl || {})[id] });
    });
    Object.keys(m.nodes).forEach(function (id) {
      var n = m.nodes[id];
      if (n.next != null && m.nodes[n.next]) links.push({ from: +id, to: n.next, hl: (x.lhl || {})[id] });
      if (n.prev != null && m.nodes[n.prev]) links.push({ from: +id, to: n.prev, back: true, hl: (x.bhl || {})[id] });
    });
    return { kind: 'list', nodes: nodes, links: links, ptrs: x.ptrs || [], vars: x.vars || {}, gap: 104 };
  }
  function one(id, cls) { var o = {}; if (id != null) o[id] = cls; return o; }
  function reset(m, vals) {
    var fresh = makeList(vals);
    Object.keys(m).forEach(function (k) { delete m[k]; });
    Object.assign(m, fresh);
  }

  DSViz.register('dl-insert', {
    title: 'insertAfter：在节点 p 之后插入',
    height: 200,
    code: [
      'DNode* insertAfter(DList* list, DNode* p, int value)',
      '{',
      '    DNode* node = malloc(sizeof(DNode));',
      '    if (node == NULL) return NULL;',
      '    node->value = value;',
      '    node->prev = p;',
      '    node->next = p->next;',
      '    if (p->next != NULL) {',
      '        p->next->prev = node;',
      '    }',
      '    p->next = node;',
      '    list->size++;',
      '    return node;',
      '}'
    ],
    init: function () { return makeList([10, 20, 30, 40]); },
    view: view,
    start: function (m, rec) {
      rec.step(0, '每个节点有两个指针：next 指向后继（上方的箭头），prev 指向前驱（下方的箭头）。输入 p 的值和要插入的值，点击「插入」。', { ptrs: [{ name: 'head', at: 0 }] });
    },
    actions: [{
      label: '插入',
      fields: [{ key: 'after', label: 'p 的值', value: 20, size: 4 }, { key: 'value', label: 'value', value: 25, size: 4 }],
      check: function (m, p) {
        if (!Number.isInteger(p.value)) return 'value 要是整数';
        if (find(m, p.after) == null && p.after !== 'H') return '链表中没有值为 ' + p.after + ' 的节点（输入 H 表示头结点）';
        if (values(m).length >= 5) return '演示区最多放 5 个元素，先删除一些吧';
        return null;
      },
      run: function (m, p, rec) {
        var pid = p.after === 'H' ? 0 : find(m, p.after);
        var ptrs = [{ name: 'p', at: pid }];
        rec.step(1, '已经有了节点 p 的指针（' + nameOf(m, pid) + '）。和单链表不同，这里不需要从头查找前驱。', { ptrs: ptrs, hl: one(pid, 'cur') });
        var id = m.nextId++;
        m.nodes[id] = { val: '?', prev: null, next: null };
        m.float[id] = { col: m.order.indexOf(pid) + 0.5, row: 1 };
        ptrs = [{ name: 'p', at: pid }, { name: 'node', at: id }];
        var hl = one(pid, 'cur'); hl[id] = 'new';
        rec.step(3, 'malloc 一个新节点。', { ptrs: ptrs, hl: hl });
        m.nodes[id].val = p.value;
        rec.step(5, '写入数据 ' + p.value + '。', { ptrs: ptrs, hl: hl });
        m.nodes[id].prev = pid;
        rec.step(6, '① node->prev = p。', { ptrs: ptrs, hl: hl, bhl: one(id, 'new') });
        var succ = m.nodes[pid].next;
        m.nodes[id].next = succ;
        rec.step(7, '② node->next = p->next（' + nameOf(m, succ) + '）。到这里只修改了新节点自己，链表还没有变。', { ptrs: ptrs, hl: hl, lhl: one(id, 'new'), vars: { 'p->next': nameOf(m, succ) } });
        if (succ != null) {
          rec.step(8, 'p 有后继，需要修改后继的 prev。', { ptrs: ptrs, hl: Object.assign(one(succ, 'cmp'), hl) });
          m.nodes[succ].prev = id;
          rec.step(9, '③ p->next->prev = node：' + nameOf(m, succ) + ' 的 prev 改为指向新节点。注意此时 p->next 还是原来的后继，所以 ③ 必须在 ④ 之前。', { ptrs: ptrs, hl: Object.assign(one(succ, 'cmp'), hl), bhl: one(succ, 'cur') });
        } else {
          rec.step(8, 'p 是最后一个节点，没有后继，跳过 ③。', { ptrs: ptrs, hl: hl });
        }
        m.nodes[pid].next = id;
        rec.step(11, '④ p->next = node。双向链表的插入一共要修改 4 个指针。', { ptrs: ptrs, hl: hl, lhl: one(pid, 'cur') });
        relayout(m);
        rec.step(12, '插入完成：' + values(m).join(' ⇄ ') + '。', { hl: one(id, 'ok'), ptrs: [{ name: 'head', at: 0 }] });
      }
    }, {
      label: '恢复',
      run: function (m, p, rec) { reset(m, [10, 20, 30, 40]); rec.step(0, '已恢复为 10 20 30 40。', { ptrs: [{ name: 'head', at: 0 }] }); }
    }]
  });

  DSViz.register('dl-remove', {
    title: 'removeNode：删除一个已知的节点',
    height: 200,
    code: [
      'void removeNode(DList* list, DNode* node)',
      '{',
      '    node->prev->next = node->next;',
      '    if (node->next != NULL) {',
      '        node->next->prev = node->prev;',
      '    }',
      '    free(node);',
      '    list->size--;',
      '}'
    ],
    init: function () { return makeList([10, 20, 30, 40]); },
    view: view,
    start: function (m, rec) { rec.step(0, '输入要删除的节点的值，点击「删除」。', { ptrs: [{ name: 'head', at: 0 }] }); },
    actions: [{
      label: '删除',
      fields: [{ key: 'value', label: 'node 的值', value: 20, size: 4 }],
      check: function (m, p) { return find(m, p.value) == null ? '链表中没有 ' + p.value : null; },
      run: function (m, p, rec) {
        var id = find(m, p.value), pr = m.nodes[id].prev, nx = m.nodes[id].next;
        var ptrs = [{ name: 'node', at: id }];
        var hl = one(id, 'del'); hl[pr] = 'cur'; if (nx != null) hl[nx] = 'cmp';
        rec.step(1, '要删除' + nameOf(m, id) + '。它的前驱（' + nameOf(m, pr) + '）可以直接通过 node->prev 得到，不需要从头查找：这就是双向链表的优势。', { ptrs: ptrs, hl: hl });
        m.nodes[pr].next = nx;
        rec.step(3, '前驱的 next 跳过 node，指向 node 的后继（' + nameOf(m, nx) + '）。', { ptrs: ptrs, hl: hl, lhl: one(pr, 'cur') });
        if (nx != null) {
          m.nodes[nx].prev = pr;
          rec.step(5, '后继的 prev 跳过 node，指向 node 的前驱。', { ptrs: ptrs, hl: hl, bhl: one(nx, 'cur') });
        } else {
          rec.step(4, 'node 是最后一个节点，没有后继需要修改。', { ptrs: ptrs, hl: hl });
        }
        var idx = m.order.indexOf(id);
        m.order[idx] = -1;
        m.float[id] = { col: idx, row: 1 };
        m.nodes[id].next = m.nodes[id].prev = null;
        rec.step(7, 'node 已经从链表中断开（这里为了画面清楚，把它移到了下方，并且没有画出它残留的指针）。free(node)。', { ptrs: ptrs, hl: one(id, 'del') });
        delete m.nodes[id];
        relayout(m);
        rec.step(8, '删除完成，整个过程是 O(1) 的：' + (values(m).join(' ⇄ ') || '（空链表）') + '。', { ptrs: [{ name: 'head', at: 0 }] });
      }
    }, {
      label: '恢复',
      run: function (m, p, rec) { reset(m, [10, 20, 30, 40]); rec.step(0, '已恢复为 10 20 30 40。', { ptrs: [{ name: 'head', at: 0 }] }); }
    }]
  });

  // ---------- 约瑟夫问题 ----------
  function ring(n) {
    var m = { n: n, next: {}, out: {}, order: [], cx: 360, cy: 120, r: 82 };
    for (var i = 1; i <= n; i++) m.next[i] = i === n ? 1 : i + 1;
    return m;
  }
  function jview(m, x) {
    var nodes = [], edges = [];
    for (var i = 1; i <= m.n; i++) {
      var ang = -Math.PI / 2 + 2 * Math.PI * (i - 1) / m.n;
      var hl = (x.hl || {})[i] || (m.out[i] ? 'dim' : null);
      nodes.push({ id: i, label: i, x: m.cx + m.r * Math.cos(ang), y: m.cy + m.r * Math.sin(ang), hl: hl, note: (x.note || {})[i] || (m.out[i] ? '第 ' + m.out[i] + ' 个出列' : null) });
    }
    for (var j = 1; j <= m.n; j++) {
      if (!m.out[j] && m.next[j] != null && m.next[j] !== j) edges.push({ a: j, b: m.next[j], directed: true, curve: m.next[j] === (j % m.n) + 1 ? 6 : 14, hl: (x.ehl || {})[j] });
    }
    return { kind: 'graph', nodes: nodes, edges: edges, vars: x.vars || {} };
  }

  DSViz.register('josephus', {
    title: '约瑟夫问题：n 个人围成一圈，数到 k 的人出列',
    height: 250,
    code: [
      'Node* prev = tail;',
      'while (prev->next != prev) {',
      '    for (int i = 1; i < k; ++i) {',
      '        prev = prev->next;',
      '    }',
      '    Node* out = prev->next;',
      '    printf(" %d", out->value);',
      '    prev->next = out->next;',
      '    free(out);',
      '}',
      'int last = prev->value;'
    ],
    init: function () { return ring(7); },
    view: jview,
    start: function (m, rec) { rec.step(0, '7 个人编号 1 到 7，用循环链表连成一圈：7 的 next 指回 1。设定 n 和 k，点击「开始」。'); },
    actions: [{
      label: '开始',
      fields: [{ key: 'n', label: 'n', value: 7, size: 3 }, { key: 'k', label: 'k', value: 3, size: 3 }],
      check: function (m, p) {
        if (!Number.isInteger(p.n) || p.n < 2 || p.n > 12) return 'n 的范围是 2 到 12';
        if (!Number.isInteger(p.k) || p.k < 1 || p.k > 9) return 'k 的范围是 1 到 9';
        return null;
      },
      run: function (m, p, rec) {
        var fresh = ring(p.n);
        Object.keys(m).forEach(function (k) { delete m[k]; });
        Object.assign(m, fresh);
        var prev = p.n, outs = [], alive = p.n;
        function vars() { return { '出列顺序': outs.join(' ') || '（无）', '剩余人数': alive }; }
        rec.step(1, 'prev 从最后一个人（' + p.n + '）开始，这样 prev->next 就是第一个报数的人。', { hl: one(prev, 'cur'), vars: vars() });
        while (m.next[prev] !== prev) {
          var note = {};
          note[m.next[prev]] = '报 1';
          for (var i = 1; i < p.k; ++i) {
            prev = m.next[prev];
            note = {}; note[prev] = '报 ' + i;
            rec.step(4, prev + ' 号报数 ' + i + '，prev 移到 ' + prev + ' 号。', { hl: one(prev, 'cur'), note: note, vars: vars() });
          }
          var out = m.next[prev];
          var hl = one(prev, 'cur'); hl[out] = 'del';
          note = {}; note[out] = '报 ' + p.k;
          rec.step(6, (out) + ' 号报数 ' + p.k + '，出列。', { hl: hl, note: note, ehl: one(prev, 'cur'), vars: vars() });
          m.next[prev] = m.next[out];
          outs.push(out);
          alive--;
          m.out[out] = outs.length;
          rec.step(8, 'prev->next = out->next：' + prev + ' 号直接连到 ' + m.next[prev] + ' 号，' + out + ' 号离开了圈子，然后被释放。', { hl: one(prev, 'cur'), ehl: one(prev, 'new'), vars: vars() });
        }
        rec.step(11, '圈中只剩下 ' + prev + ' 号（它的 next 指向自己），循环结束。出列顺序：' + outs.join(' ') + '，最后剩下 ' + prev + ' 号。', { hl: one(prev, 'ok'), vars: vars() });
      }
    }]
  });
})();
