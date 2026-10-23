/* 《二叉搜索树》：查找与插入、删除的三种情况、有序插入导致的退化 */
(function () {
  'use strict';

  function makeTree(keys) {
    var m = { nodes: {}, root: null, nextId: 1 };
    keys.forEach(function (k) { insertPlain(m, k); });
    return m;
  }
  function insertPlain(m, key) {
    var id = m.nextId++;
    m.nodes[id] = { id: id, val: key, left: null, right: null };
    if (m.root == null) { m.root = id; return id; }
    var p = m.root;
    for (;;) {
      var n = m.nodes[p];
      if (key === n.val) { delete m.nodes[id]; return null; }
      var side = key < n.val ? 'left' : 'right';
      if (n[side] == null) { n[side] = id; return id; }
      p = n[side];
    }
  }
  function view(m, x) {
    var list = Object.keys(m.nodes).map(function (k) {
      var n = m.nodes[k];
      return { id: n.id, val: n.val, left: n.left, right: n.right, hl: (x.hl || {})[n.id], sub: (x.sub || {})[n.id] };
    });
    return { kind: 'tree', nodes: list, root: m.root, treeWidth: 700, top: 40, treeHeight: x.treeHeight || 250, maxGap: 52, ptrs: x.ptrs || [], vars: x.vars || {} };
  }
  function one(id, c) { var o = {}; if (id != null) o[id] = c; return o; }
  function intCheck(k) { return function (m, p) { return Number.isInteger(p[k]) && Math.abs(p[k]) < 1000 ? null : '请输入 -999 到 999 之间的整数'; }; }
  var INIT = [50, 30, 70, 20, 40, 60, 80, 35, 45];
  function reset(m, keys) {
    var fresh = makeTree(keys);
    Object.keys(m).forEach(function (k) { delete m[k]; });
    Object.assign(m, fresh);
  }

  DSViz.register('bst-insert', {
    title: '二叉搜索树的查找和插入',
    height: 260,
    code: [
      'Node* search(Node* root, int key)',
      '{',
      '    Node* p = root;',
      '    while (p != NULL && p->key != key) {',
      '        p = key < p->key ? p->left : p->right;',
      '    }',
      '    return p;',
      '}',
      '',
      'int insert(Node** root, int key)',
      '{',
      '    Node** link = root;',
      '    while (*link != NULL) {',
      '        if (key == (*link)->key) return 0;',
      '        link = key < (*link)->key ? &(*link)->left : &(*link)->right;',
      '    }',
      '    Node* node = malloc(sizeof(Node));',
      '    node->key = key; node->left = node->right = NULL;',
      '    *link = node;',
      '    return 1;',
      '}'
    ],
    init: function () { return makeTree(INIT); },
    view: view,
    start: function (m, rec) { rec.step(0, '每个节点左子树中的值都比它小，右子树中的值都比它大。试试查找 45、查找 55、插入 65。'); },
    actions: [{
      label: '查找',
      fields: [{ key: 'key', label: 'key', value: 45, size: 4 }],
      check: intCheck('key'),
      run: function (m, p, rec) {
        var cur = m.root, path = {}, cmp = 0;
        rec.step(3, 'p 从根开始。', { ptrs: cur != null ? [{ name: 'p', at: cur }] : [], hl: one(cur, 'cur') });
        while (cur != null && m.nodes[cur].val !== p.key) {
          var v = m.nodes[cur].val; cmp++;
          path[cur] = 'dim';
          var next = p.key < v ? m.nodes[cur].left : m.nodes[cur].right;
          rec.step(5, p.key + (p.key < v ? ' < ' : ' > ') + v + '，往' + (p.key < v ? '左' : '右') + '走' + (next == null ? '，是 NULL。' : '。另一侧的整棵子树都不用看了。'), { ptrs: [{ name: 'p', at: cur }], hl: Object.assign({}, path, one(cur, 'cmp')), vars: { '比较次数': cmp } });
          cur = next;
          if (cur != null) rec.step(4, '来到 ' + m.nodes[cur].val + '。', { ptrs: [{ name: 'p', at: cur }], hl: Object.assign({}, path, one(cur, 'cur')), vars: { '比较次数': cmp } });
        }
        if (cur == null) rec.step(7, 'p 变成了 NULL：树中没有 ' + p.key + '，返回 NULL。比较了 ' + cmp + ' 次，最多等于树的高度。', { hl: path, vars: { '比较次数': cmp } });
        else rec.step(7, '找到了 ' + p.key + '。每次比较都沿着一条路径向下走一层，比较次数不超过树的高度。', { ptrs: [{ name: 'p', at: cur }], hl: Object.assign({}, path, one(cur, 'ok')), vars: { '比较次数': cmp + 1 } });
      }
    }, {
      label: '插入',
      fields: [{ key: 'key', label: 'key', value: 65, size: 4 }],
      check: function (m, p) { return intCheck('key')(m, p) || (Object.keys(m.nodes).length >= 15 ? '演示最多 15 个节点，先恢复一下吧' : null); },
      run: function (m, p, rec) {
        var cur = m.root, path = {}, parent = null, side = null;
        rec.step(12, '和查找一样，从根开始往下走，找到新节点应该挂的位置。', { hl: one(cur, 'cur') });
        while (cur != null) {
          var v = m.nodes[cur].val;
          if (p.key === v) { rec.step(14, p.key + ' 已经存在，不重复插入，返回 0。', { hl: Object.assign({}, path, one(cur, 'warn')) }); return; }
          path[cur] = 'dim';
          side = p.key < v ? 'left' : 'right';
          rec.step(15, p.key + (p.key < v ? ' < ' : ' > ') + v + '，去' + (side === 'left' ? '左' : '右') + '子树。', { hl: Object.assign({}, path, one(cur, 'cmp')) });
          parent = cur;
          cur = m.nodes[cur][side];
        }
        var id = m.nextId++;
        m.nodes[id] = { id: id, val: p.key, left: null, right: null };
        if (parent == null) m.root = id; else m.nodes[parent][side] = id;
        rec.step(19, '走到了空位置：' + (parent == null ? '树是空的，新节点成为根。' : m.nodes[parent].val + ' 的' + (side === 'left' ? '左' : '右') + '孩子。') + '新节点总是作为叶子插入，已有的节点都不需要移动。', { hl: Object.assign({}, path, one(id, 'new')) });
      }
    }, {
      label: '恢复',
      run: function (m, p, rec) { reset(m, INIT); rec.step(0, '已恢复。'); }
    }]
  });

  DSViz.register('bst-remove', {
    title: '删除节点的三种情况',
    height: 260,
    code: [
      'Node* target = *link;          /* 已经找到要删除的节点 */',
      'if (target->left != NULL && target->right != NULL) {',
      '    Node** succLink = &target->right;',
      '    while ((*succLink)->left != NULL) {',
      '        succLink = &(*succLink)->left;',
      '    }',
      '    Node* succ = *succLink;',
      '    target->key = succ->key;',
      '    *succLink = succ->right;',
      '    free(succ);',
      '} else {',
      '    *link = target->left != NULL ? target->left : target->right;',
      '    free(target);',
      '}'
    ],
    init: function () { return makeTree(INIT); },
    view: view,
    start: function (m, rec) { rec.step(0, '依次试试删除 20（叶子）、30（只有一个孩子）、50（有两个孩子）。'); },
    actions: [{
      label: '删除',
      fields: [{ key: 'key', label: 'key', value: 20, size: 4 }],
      check: intCheck('key'),
      run: function (m, p, rec) {
        var cur = m.root, parent = null, side = null, path = {};
        while (cur != null && m.nodes[cur].val !== p.key) {
          path[cur] = 'dim';
          parent = cur;
          side = p.key < m.nodes[cur].val ? 'left' : 'right';
          cur = m.nodes[cur][side];
        }
        if (cur == null) { rec.step(1, '沿着查找路径走到了 NULL，树中没有 ' + p.key + '。', { hl: path }); return; }
        var t = m.nodes[cur];
        rec.step(1, '先像查找一样找到 ' + p.key + '（灰色是走过的路径），同时记下指向它的那个指针。', { hl: Object.assign({}, path, one(cur, 'del')), ptrs: [{ name: 'target', at: cur }] });
        function setLink(child) { if (parent == null) m.root = child; else m.nodes[parent][side] = child; }
        if (t.left != null && t.right != null) {
          rec.step(2, '情况 3：有两个孩子。不能直接删掉它，否则两棵子树没有地方挂。找它的后继：右子树中最小的节点。', { hl: one(cur, 'del'), ptrs: [{ name: 'target', at: cur }] });
          var sp = cur, sside = 'right', s = t.right;
          rec.step(3, '先走到右孩子 ' + m.nodes[s].val + '。', { hl: Object.assign(one(cur, 'del'), one(s, 'cmp')), ptrs: [{ name: 'target', at: cur }, { name: 'succ', at: s }] });
          while (m.nodes[s].left != null) {
            sp = s; sside = 'left'; s = m.nodes[s].left;
            rec.step(5, '一直向左走：' + m.nodes[s].val + '。', { hl: Object.assign(one(cur, 'del'), one(s, 'cmp')), ptrs: [{ name: 'target', at: cur }, { name: 'succ', at: s }] });
          }
          rec.step(7, '后继是 ' + m.nodes[s].val + '：比 ' + t.val + ' 大的数中最小的一个。它一定没有左孩子。', { hl: Object.assign(one(cur, 'del'), one(s, 'cur')), ptrs: [{ name: 'target', at: cur }, { name: 'succ', at: s }] });
          t.val = m.nodes[s].val;
          rec.step(8, '用后继的值 ' + t.val + ' 覆盖要删除的节点。现在树中有两个 ' + t.val + '，只要删掉原来那个后继就行了。', { hl: Object.assign(one(cur, 'new'), one(s, 'del')), ptrs: [{ name: 'succ', at: s }] });
          m.nodes[sp][sside] = m.nodes[s].right;
          delete m.nodes[s];
          rec.step(10, '后继最多只有一个右孩子，按情况 1 或 2 删除它：让它的父节点直接指向它的右孩子。中序遍历的顺序保持不变。', { hl: one(cur, 'ok') });
        } else {
          var child = t.left != null ? t.left : t.right;
          rec.step(12, child == null ? '情况 1：是叶子，直接删除，父节点中指向它的指针改为 NULL。' : '情况 2：只有一个孩子（' + m.nodes[child].val + '），让这个孩子顶替它的位置。', { hl: Object.assign(one(cur, 'del'), one(child, 'cmp')), ptrs: [{ name: 'target', at: cur }] });
          setLink(child);
          delete m.nodes[cur];
          rec.step(13, '删除完成。', { hl: one(child, 'ok') });
        }
      }
    }, {
      label: '恢复',
      run: function (m, p, rec) { reset(m, INIT); rec.step(0, '已恢复。'); }
    }]
  });

  DSViz.register('bst-degenerate', {
    title: '插入的顺序决定了树的形状',
    height: 300,
    init: function () { return makeTree([]); },
    view: function (m, x) { return Object.assign(view(m, x), { treeHeight: 290, vars: { '节点数': Object.keys(m.nodes).length, '高度': height(m) } }); },
    start: function (m, rec) { rec.step(0, '同样是 1 到 9 这 9 个数，按不同的顺序插入，得到的树完全不同。'); },
    actions: [['有序插入 1..9', [1, 2, 3, 4, 5, 6, 7, 8, 9]], ['打乱后插入', [5, 2, 8, 1, 4, 7, 9, 3, 6]]].map(function (a) {
      return {
        label: a[0],
        run: function (m, p, rec) {
          reset(m, []);
          a[1].forEach(function (k) {
            var id = insertPlain(m, k);
            rec.step(0, '插入 ' + k + '。', { hl: one(id, 'new') });
          });
          var h = height(m);
          rec.step(0, h === 9 ? '每个新节点都比前面的大，全部挂在右边：树退化成了一条链表，高度是 9，查找变成了 O(n)。' : '高度只有 ' + h + '。随机的插入顺序得到的树比较「矮胖」，查找是 O(log n)。');
        }
      };
    })
  });

  function height(m) {
    function h(id) { if (id == null) return 0; var n = m.nodes[id]; return 1 + Math.max(h(n.left), h(n.right)); }
    return h(m.root);
  }
})();
