/* 单链表的逐步演示：和《单链表》正文中的 C 代码逐行对应 */
(function () {
  'use strict';

  // ---------- 链表模型 ----------
  // 节点 0 是头结点；order 是当前从头结点出发能走到的节点顺序，用来排列横坐标
  function makeList(values) {
    var m = { nodes: { 0: { val: 'H', next: null } }, nextId: 1, order: [0], float: {} };
    var prev = 0;
    values.forEach(function (v) {
      var id = m.nextId++;
      m.nodes[id] = { val: v, next: null };
      m.nodes[prev].next = id;
      prev = id;
    });
    relayout(m);
    return m;
  }
  function relayout(m) {
    m.order = [];
    var seen = {};
    for (var p = 0; p != null && !seen[p]; p = m.nodes[p].next) { seen[p] = true; m.order.push(p); }
    m.float = {};
  }
  function values(m) {
    var out = [];
    for (var p = m.nodes[0].next; p != null; p = m.nodes[p].next) out.push(m.nodes[p].val);
    return out;
  }
  function view(m, x) {
    var nodes = [], links = [];
    m.order.forEach(function (id, i) {
      if (!m.nodes[id]) return;
      nodes.push({ id: id, val: m.nodes[id].val, col: i, row: 0, hl: (x.hl || {})[id], sub: id === 0 ? '头结点' : null });
    });
    Object.keys(m.float).forEach(function (id) {
      if (!m.nodes[id]) return;
      var f = m.float[id];
      nodes.push({ id: +id, val: m.nodes[id].val, col: f.col, row: f.row, hl: (x.hl || {})[id] });
    });
    Object.keys(m.nodes).forEach(function (id) {
      var n = m.nodes[id];
      if (n.next != null && m.nodes[n.next]) links.push({ from: +id, to: n.next, hl: (x.lhl || {})[id] });
    });
    return { kind: 'list', nodes: nodes, links: links, ptrs: x.ptrs || [], vars: x.vars || {} };
  }
  function nameOf(m, id) { return id == null ? 'NULL' : (id === 0 ? '头结点' : '节点 ' + m.nodes[id].val); }

  // ---------- 插入 ----------
  DSViz.register('ll-insert', {
    title: 'insertAt：在第 pos 个位置插入',
    height: 200,
    code: [
      'int insertAt(List* list, size_t pos, int value)',
      '{',
      '    if (pos > list->size) return 0;',
      '    Node* prev = list->head;',
      '    for (size_t i = 0; i < pos; ++i) {',
      '        prev = prev->next;',
      '    }',
      '    Node* node = malloc(sizeof(Node));',
      '    if (node == NULL) return 0;',
      '    node->value = value;',
      '    node->next = prev->next;',
      '    prev->next = node;',
      '    list->size++;',
      '    return 1;',
      '}'
    ],
    init: function () { return makeList([10, 20, 30, 40]); },
    view: view,
    start: function (m, rec) {
      rec.step(0, '链表中有 4 个元素。输入位置和值，点击「插入」开始演示；也可以用下面的按钮单步查看。', { ptrs: [{ name: 'head', at: 0 }] });
    },
    actions: [{
      label: '插入',
      fields: [{ key: 'pos', label: 'pos', value: 2, size: 3 }, { key: 'value', label: 'value', value: 25, size: 4 }],
      check: function (m, p) {
        var size = values(m).length;
        if (!Number.isInteger(p.pos) || p.pos < 0) return 'pos 要是非负整数';
        if (!Number.isInteger(p.value)) return 'value 要是整数';
        if (size >= 6 && p.pos <= size) return '演示区最多放 6 个元素，先用「删除」演示删掉一些吧';
        return null;
      },
      run: function (m, p, rec) {
        var size = values(m).length;
        var v = { pos: p.pos, size: size };
        rec.step(3, '检查位置：pos = ' + p.pos + '，size = ' + size + '。', { vars: v });
        if (p.pos > size) {
          rec.step(3, 'pos 大于 size，位置超出范围，返回 0。合法的位置是 0 到 ' + size + '（等于 size 表示插在末尾）。', { vars: v });
          return;
        }
        var prev = 0;
        rec.step(4, 'prev 从头结点出发。有了头结点，在第 0 个位置插入也不需要特殊处理。', { ptrs: [{ name: 'prev', at: prev }], hl: { 0: 'cur' }, vars: { pos: p.pos, prev: nameOf(m, prev) } });
        for (var i = 0; i < p.pos; ++i) {
          rec.step(5, 'i = ' + i + ' < pos，继续向后走。', { ptrs: [{ name: 'prev', at: prev }], hl: (function () { var o = {}; o[prev] = 'cur'; return o; })(), vars: { i: i, pos: p.pos, prev: nameOf(m, prev) } });
          prev = m.nodes[prev].next;
          rec.step(6, 'prev = prev->next，prev 来到' + nameOf(m, prev) + '。', { ptrs: [{ name: 'prev', at: prev }], hl: (function () { var o = {}; o[prev] = 'cur'; return o; })(), vars: { i: i, pos: p.pos, prev: nameOf(m, prev) } });
        }
        var hlPrev = {}; hlPrev[prev] = 'cur';
        rec.step(5, 'i = ' + p.pos + '，循环结束。prev 停在第 ' + p.pos + ' 个位置的前一个节点：' + nameOf(m, prev) + '。', { ptrs: [{ name: 'prev', at: prev }], hl: hlPrev, vars: { i: p.pos, prev: nameOf(m, prev) } });

        var id = m.nextId++;
        var col = m.order.indexOf(prev) + 0.5;
        m.nodes[id] = { val: '?', next: null };
        m.float[id] = { col: col, row: 1 };
        var hl = {}; hl[prev] = 'cur'; hl[id] = 'new';
        rec.step(8, 'malloc 一个新节点。它还没有接入链表，next 也还没有设置。', { ptrs: [{ name: 'prev', at: prev }, { name: 'node', at: id }], hl: hl, vars: { prev: nameOf(m, prev), node: '新节点' } });
        m.nodes[id].val = p.value;
        rec.step(10, '写入数据 ' + p.value + '。', { ptrs: [{ name: 'prev', at: prev }, { name: 'node', at: id }], hl: hl, vars: { prev: nameOf(m, prev), 'node->value': p.value } });
        m.nodes[id].next = m.nodes[prev].next;
        var lhl = {}; lhl[id] = 'new';
        rec.step(11, '① node->next = prev->next：新节点先指向 prev 原来的后继（' + nameOf(m, m.nodes[id].next) + '）。此时链表本身还没有变化。', { ptrs: [{ name: 'prev', at: prev }, { name: 'node', at: id }], hl: hl, lhl: lhl, vars: { 'node->next': nameOf(m, m.nodes[id].next) } });
        m.nodes[prev].next = id;
        lhl = {}; lhl[prev] = 'cur'; lhl[id] = 'new';
        rec.step(12, '② prev->next = node：前驱改为指向新节点，新节点正式接入链表。注意 ① ② 的顺序不能颠倒。', { ptrs: [{ name: 'prev', at: prev }, { name: 'node', at: id }], hl: hl, lhl: lhl, vars: { 'prev->next': '新节点' } });
        relayout(m);
        var done = {}; done[id] = 'ok';
        rec.step(13, 'size 加 1。重新排列之后：' + values(m).join(' → ') + '。整个过程只修改了两个指针，没有移动任何元素。', { hl: done, ptrs: [{ name: 'head', at: 0 }], vars: { size: values(m).length } });
      }
    }]
  });

  // ---------- 删除 ----------
  DSViz.register('ll-remove', {
    title: 'removeValue：删除第一个等于 value 的节点',
    height: 200,
    code: [
      'int removeValue(List* list, int value)',
      '{',
      '    Node* prev = list->head;',
      '    while (prev->next != NULL &&',
      '           prev->next->value != value) {',
      '        prev = prev->next;',
      '    }',
      '    if (prev->next == NULL) return 0;',
      '    Node* target = prev->next;',
      '    prev->next = target->next;',
      '    free(target);',
      '    list->size--;',
      '    return 1;',
      '}'
    ],
    init: function () { return makeList([10, 20, 30, 40, 50]); },
    view: view,
    start: function (m, rec) {
      rec.step(0, '输入要删除的值，点击「删除」开始演示。', { ptrs: [{ name: 'head', at: 0 }] });
    },
    actions: [{
      label: '删除',
      fields: [{ key: 'value', label: 'value', value: 30, size: 4 }],
      check: function (m, p) { return Number.isInteger(p.value) ? null : 'value 要是整数'; },
      run: function (m, p, rec) {
        var prev = 0;
        function st(line, msg, extra) {
          extra = extra || {};
          var hl = extra.hl || {}; if (hl[prev] == null) hl[prev] = 'cur';
          var nx = m.nodes[prev].next;
          if (nx != null && extra.cmp !== false && hl[nx] == null) hl[nx] = 'cmp';
          rec.step(line, msg, { ptrs: [{ name: 'prev', at: prev }].concat(extra.ptrs || []), hl: hl, lhl: extra.lhl, vars: extra.vars || { prev: nameOf(m, prev), 'prev->next': nameOf(m, nx) } });
        }
        st(3, 'prev 从头结点出发。删除一个节点需要修改它的前驱，所以我们让 prev 停在要删除的节点的前一个位置。', { cmp: false });
        while (m.nodes[prev].next != null && m.nodes[m.nodes[prev].next].val !== p.value) {
          st(4, '比较 prev->next 的值 ' + m.nodes[m.nodes[prev].next].val + '，不等于 ' + p.value + '，继续。');
          prev = m.nodes[prev].next;
          st(6, 'prev 前进到' + nameOf(m, prev) + '。', { cmp: false });
        }
        if (m.nodes[prev].next == null) {
          st(8, 'prev->next 是 NULL，已经走到链表末尾，没有找到 ' + p.value + '，返回 0。');
          return;
        }
        var target = m.nodes[prev].next;
        st(4, 'prev->next 的值等于 ' + p.value + '，找到了，循环结束。');
        var hl = {}; hl[target] = 'del';
        st(9, 'target 指向要删除的节点。', { hl: hl, ptrs: [{ name: 'target', at: target }], vars: { target: nameOf(m, target) } });
        m.nodes[prev].next = m.nodes[target].next;
        var idx = m.order.indexOf(target);
        m.order.splice(idx, 1);
        m.float[target] = { col: idx, row: 1 };
        // 让其余节点保持原来的位置：在 order 中留一个空位
        m.order.splice(idx, 0, -1);
        var lhl = {}; lhl[prev] = 'cur';
        st(10, 'prev->next = target->next：前驱直接指向 target 的后继，target 被「绕过」，已经不在链表中了。', { hl: hl, lhl: lhl, ptrs: [{ name: 'target', at: target }], vars: { 'prev->next': nameOf(m, m.nodes[prev].next) } });
        delete m.nodes[target];
        st(11, 'free(target)：释放这个节点的内存。如果先 free 再修改 prev->next，就会访问已经释放的内存。', { cmp: false, vars: { target: '（已释放）' } });
        relayout(m);
        rec.step(12, 'size 减 1。删除之后：' + (values(m).join(' → ') || '（空链表）') + '。', { ptrs: [{ name: 'head', at: 0 }], vars: { size: values(m).length } });
      }
    }, {
      label: '恢复为 10 20 30 40 50',
      run: function (m, p, rec) {
        var fresh = makeList([10, 20, 30, 40, 50]);
        Object.keys(m).forEach(function (k) { delete m[k]; });
        Object.assign(m, fresh);
        rec.step(0, '已恢复。', { ptrs: [{ name: 'head', at: 0 }] });
      }
    }]
  });

  // ---------- 反转 ----------
  DSViz.register('ll-reverse', {
    title: 'reverse：原地反转链表',
    height: 190,
    code: [
      'void reverse(List* list)',
      '{',
      '    Node* prev = NULL;',
      '    Node* cur = list->head->next;',
      '    while (cur != NULL) {',
      '        Node* next = cur->next;',
      '        cur->next = prev;',
      '        prev = cur;',
      '        cur = next;',
      '    }',
      '    list->head->next = prev;',
      '}'
    ],
    init: function () { return makeList([1, 2, 3, 4]); },
    view: view,
    start: function (m, rec) {
      rec.step(0, '点击「反转」开始演示。反转时只修改每个节点的 next 指针，节点本身不移动。', { ptrs: [{ name: 'head', at: 0 }] });
    },
    actions: [{
      label: '反转',
      run: function (m, p, rec) {
        var prev = null, cur = m.nodes[0].next, next = null;
        function st(line, msg, lhl) {
          var ptrs = [], hl = {};
          if (prev != null) ptrs.push({ name: 'prev', at: prev });
          if (cur != null) { ptrs.push({ name: 'cur', at: cur }); hl[cur] = 'cur'; }
          if (next != null) ptrs.push({ name: 'next', at: next });
          rec.step(line, msg, { ptrs: ptrs, hl: hl, lhl: lhl, vars: { prev: nameOf(m, prev), cur: nameOf(m, cur), next: nameOf(m, next) } });
        }
        m.nodes[0].next = null;                    // 演示中先断开头结点，最后再接回去，画面更清楚
        var first = cur;
        // 为了让画面保持原来的位置，order 不重新计算
        m.order = [0];
        for (var p0 = first; p0 != null; p0 = m.nodes[p0].next) m.order.push(p0);
        m.nodes[0].next = first;
        st(3, 'prev = NULL：反转之后，原来的第一个节点会变成最后一个，它的 next 应该是 NULL。');
        st(4, 'cur 指向第一个元素。');
        while (cur != null) {
          st(5, 'cur 不是 NULL，进入循环。');
          next = m.nodes[cur].next;
          st(6, '先用 next 记住 cur 的后继（' + nameOf(m, next) + '）。下一步修改 cur->next 之后，就再也找不到它了。');
          m.nodes[cur].next = prev;
          var lhl = {}; lhl[cur] = 'new';
          st(7, 'cur->next = prev：' + nameOf(m, cur) + ' 改为指向' + (prev == null ? ' NULL（指针域中的斜线）' : nameOf(m, prev)) + '。', lhl);
          prev = cur;
          st(8, 'prev 前进到 cur。');
          cur = next;
          next = null;
          st(9, 'cur 前进到原来的后继' + (cur == null ? '，也就是 NULL' : '') + '。');
        }
        m.nodes[0].next = prev;
        var lh = {}; lh[0] = 'cur';
        st(11, '循环结束，prev 指向原来的最后一个节点，让头结点指向它。');
        relayout(m);
        rec.step(11, '重新排列：' + values(m).join(' → ') + '。每个节点只访问了一次，时间复杂度 O(n)，额外空间 O(1)。', { ptrs: [{ name: 'head', at: 0 }] });
      }
    }, {
      label: '恢复为 1 2 3 4',
      run: function (m, p, rec) {
        var fresh = makeList([1, 2, 3, 4]);
        Object.keys(m).forEach(function (k) { delete m[k]; });
        Object.assign(m, fresh);
        rec.step(0, '已恢复。', { ptrs: [{ name: 'head', at: 0 }] });
      }
    }]
  });

  // ---------- 快慢指针 ----------
  function fastSlow(name, title, code, valuesInit, withCycle) {
    DSViz.register(name, {
      title: title,
      height: withCycle ? 210 : 190,
      code: code,
      init: function () {
        var m = makeList(valuesInit);
        if (withCycle) {
          var ids = m.order.slice(1);
          m.nodes[ids[ids.length - 1]].next = ids[2];     // 最后一个节点指回第 3 个
        }
        return m;
      },
      view: view,
      start: function (m, rec) {
        rec.step(0, withCycle ? '最后一个节点指回了第 3 个节点，形成一个环。点击「开始」。' : '点击「开始」。慢指针每次走一步，快指针每次走两步。', { ptrs: [{ name: 'head', at: 0 }] });
      },
      actions: [{
        label: '开始',
        run: function (m, p, rec) {
          var slow = m.nodes[0].next, fast = slow, round = 0;
          function st(line, msg) {
            var hl = {};
            if (slow != null) hl[slow] = 'cur';
            if (fast != null) hl[fast] = slow === fast ? 'ok' : 'cmp';
            var ptrs = [];
            if (slow != null) ptrs.push({ name: 'slow', at: slow });
            if (fast != null) ptrs.push({ name: 'fast', at: fast, hl: 'warn' });
            rec.step(line, msg, { ptrs: ptrs, hl: hl, vars: { '轮数': round, slow: nameOf(m, slow), fast: nameOf(m, fast) } });
          }
          st(withCycle ? 3 : 3, '两个指针都从第一个元素出发。');
          while (fast != null && m.nodes[fast].next != null) {
            round++;
            slow = m.nodes[slow].next;
            st(6, '慢指针走一步。');
            fast = m.nodes[m.nodes[fast].next].next;
            st(7, '快指针走两步。');
            if (withCycle && slow === fast) {
              st(8, '快指针追上了慢指针（' + nameOf(m, slow) + '），说明链表中有环，返回 1。如果没有环，快指针会先到达 NULL。');
              return;
            }
            if (round > 20) return;
          }
          if (withCycle) st(10, 'fast 到达了末尾，没有环，返回 0。');
          else st(9, (fast == null ? 'fast 变成了 NULL' : 'fast->next 是 NULL') + '，循环结束。快指针走到末尾时，慢指针正好走到一半：中间节点是 ' + nameOf(m, slow) + '。');
        }
      }]
    });
  }
  fastSlow('ll-middle', 'middle：用快慢指针找中间节点', [
    'Node* middle(const List* list)',
    '{',
    '    Node* slow = list->head->next;',
    '    Node* fast = list->head->next;',
    '    while (fast != NULL && fast->next != NULL) {',
    '        slow = slow->next;',
    '        fast = fast->next->next;',
    '    }',
    '    return slow;',
    '}'
  ], [1, 2, 3, 4, 5], false);
  fastSlow('ll-cycle', 'hasCycle：判断链表中是否有环', [
    'int hasCycle(const List* list)',
    '{',
    '    Node* slow = list->head->next;',
    '    Node* fast = list->head->next;',
    '    while (fast != NULL && fast->next != NULL) {',
    '        slow = slow->next;',
    '        fast = fast->next->next;',
    '        if (slow == fast) return 1;',
    '    }',
    '    return 0;',
    '}'
  ], [1, 2, 3, 4, 5, 6], true);
})();
