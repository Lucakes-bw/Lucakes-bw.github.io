/* 《二叉树与遍历》：递归遍历（显示调用栈）、层序遍历（显示队列）、非递归中序遍历（显示栈） */
(function () {
  'use strict';

  // 按前序描述建树，'#' 表示空树
  function build(desc) {
    var nodes = {}, pos = 0, id = 0, ok = true;
    function rec() {
      if (pos >= desc.length) { ok = false; return null; }
      var c = desc[pos++];
      if (c === '#') return null;
      var my = ++id;
      nodes[my] = { id: my, val: c, left: null, right: null };
      nodes[my].left = rec();
      nodes[my].right = rec();
      return my;
    }
    var root = rec();
    if (pos !== desc.length) ok = false;
    return ok ? { nodes: nodes, root: root, desc: desc } : null;
  }
  function treePart(m, x) {
    var list = Object.keys(m.nodes).map(function (k) {
      var n = m.nodes[k];
      return { id: n.id, val: n.val, left: n.left, right: n.right, hl: (x.hl || {})[n.id] };
    });
    return { kind: 'tree', nodes: list, root: m.root, treeWidth: 470, top: 44, treeHeight: 230, maxGap: 62, ptrs: x.ptrs || [] };
  }
  function row(vals, top, label, hl) {
    return { kind: 'array', x0: 20, cellW: 40, top: top, label: label, noIndex: true, cells: vals.map(function (v, i) { return { v: v, hl: (hl || {})[i] }; }) };
  }
  function vstack(items, label, topHl) {
    var cells = items.map(function (v, i) { return { v: v, hl: i === items.length - 1 ? topHl : null }; });
    return { kind: 'array', vertical: true, x0: 560, top: 40, cap: 5, cellW: 116, noIndex: true, label: label, cells: cells.slice(-5), ptrs: [] };
  }
  var DEFAULT = 'ABD##E##CF###';
  function descField() { return { key: 'desc', label: '前序描述', value: DEFAULT, size: 16 }; }
  function checkDesc(m, p) {
    var d = String(p.desc).replace(/\s+/g, '');
    if (d.length > 31) return '描述太长了（最多 31 个字符）';
    var t = build(d);
    if (!t) return '描述不完整：每个节点后面都要给出左右子树，空树用 # 表示';
    if (Object.keys(t.nodes).length > 12) return '演示最多 12 个节点';
    return null;
  }
  function load(m, p) {
    var t = build(String(p.desc).replace(/\s+/g, ''));
    m.nodes = t.nodes; m.root = t.root; m.desc = t.desc;
  }

  // ---------- 递归遍历 ----------
  var LINES = { pre: { enter: 1, nul: 3, visit: 4, left: 5, right: 6, end: 7 }, in: { enter: 9, nul: 11, left: 12, visit: 13, right: 14, end: 15 }, post: { enter: 17, nul: 19, left: 20, right: 21, visit: 22, end: 23 } };
  var NAME = { pre: 'preorder', in: 'inorder', post: 'postorder' };

  DSViz.register('tree-recursive', {
    title: '递归遍历：前序、中序、后序',
    height: 330,
    code: [
      'void preorder(const TreeNode* root)',
      '{',
      '    if (root == NULL) return;',
      '    printf("%c ", root->value);',
      '    preorder(root->left);',
      '    preorder(root->right);',
      '}',
      '',
      'void inorder(const TreeNode* root)',
      '{',
      '    if (root == NULL) return;',
      '    inorder(root->left);',
      '    printf("%c ", root->value);',
      '    inorder(root->right);',
      '}',
      '',
      'void postorder(const TreeNode* root)',
      '{',
      '    if (root == NULL) return;',
      '    postorder(root->left);',
      '    postorder(root->right);',
      '    printf("%c ", root->value);',
      '}'
    ],
    init: function () { return build(DEFAULT); },
    view: function (m, x) {
      return {
        kind: 'multi', vars: x.vars || {},
        parts: [treePart(m, x), vstack(x.stack || [], '调用栈', 'cur'), row(x.out || [], 290, '输出', x.outHl)]
      };
    },
    start: function (m, rec) { rec.step(0, '选择一种遍历方式开始。右侧是函数的调用栈：每调用一次函数压入一层，返回时弹出。也可以修改树的前序描述（# 表示空树）。'); },
    actions: ['pre', 'in', 'post'].map(function (kind) {
      return {
        label: { pre: '前序', in: '中序', post: '后序' }[kind],
        fields: kind === 'pre' ? [descField()] : [],
        check: kind === 'pre' ? checkDesc : null,
        run: function (m, p, rec) {
          if (kind === 'pre') load(m, p);
          var L = LINES[kind], stack = [], out = [], visited = {};
          function hl(cur) {
            var o = {};
            Object.keys(visited).forEach(function (k) { o[k] = 'ok'; });
            stack.forEach(function (f) { if (f.id != null && !visited[f.id]) o[f.id] = 'cmp'; });
            if (cur != null && !visited[cur]) o[cur] = 'cur';
            return o;
          }
          function st(line, msg, cur, extra) {
            extra = extra || {};
            var oh = {}; if (extra.outNew) oh[out.length - 1] = 'new';
            rec.step(line, msg, { hl: hl(cur), stack: stack.map(function (f) { return f.label; }), out: out.slice(), outHl: oh, ptrs: cur != null ? [{ name: 'root', at: cur }] : [], vars: { '递归深度': stack.length } });
          }
          function go(id) {
            var label = NAME[kind] + '(' + (id == null ? 'NULL' : m.nodes[id].val) + ')';
            stack.push({ id: id, label: label });
            if (id == null) {
              st(L.nul, '调用 ' + label + '：空树，直接返回。', stack.length > 1 ? stack[stack.length - 2].id : null);
              stack.pop();
              return;
            }
            var v = m.nodes[id].val;
            st(L.enter, '调用 ' + label + '，压入调用栈。', id);
            var order = kind === 'pre' ? ['visit', 'left', 'right'] : kind === 'in' ? ['left', 'visit', 'right'] : ['left', 'right', 'visit'];
            order.forEach(function (step) {
              if (step === 'visit') {
                out.push(v); visited[id] = true;
                st(L.visit, '访问 ' + v + '，输出。', id, { outNew: true });
              } else {
                var child = m.nodes[id][step];
                st(L[step], '递归处理 ' + v + ' 的' + (step === 'left' ? '左' : '右') + '子树。', id);
                go(child);
              }
            });
            stack.pop();
            st(L.end, label + ' 结束，从调用栈弹出，回到上一层。', stack.length ? stack[stack.length - 1].id : null);
          }
          go(m.root);
          st(0, NAME[kind] + ' 完成：' + out.join(' ') + '。', null);
        }
      };
    })
  });

  // ---------- 层序遍历 ----------
  DSViz.register('tree-level', {
    title: '层序遍历：用队列一层一层地访问',
    height: 330,
    code: [
      'queue[tail++] = root;',
      'while (head < tail) {',
      '    TreeNode* node = queue[head++];',
      '    printf("%c ", node->value);',
      '    if (node->left != NULL) queue[tail++] = node->left;',
      '    if (node->right != NULL) queue[tail++] = node->right;',
      '}'
    ],
    init: function () { return build(DEFAULT); },
    view: function (m, x) {
      return {
        kind: 'multi', vars: x.vars || {},
        parts: [Object.assign(treePart(m, x), { treeWidth: 700 }), row(x.queue || [], 290 - 90, '队列（左边是队头）', x.qHl), row(x.out || [], 290, '输出', x.outHl)]
      };
    },
    start: function (m, rec) { rec.step(0, '先访问根，再访问第 2 层的所有节点，然后是第 3 层……点击「开始」。'); },
    actions: [{
      label: '开始',
      fields: [descField()],
      check: checkDesc,
      run: function (m, p, rec) {
        load(m, p);
        var q = [], out = [], done = {};
        function st(line, msg, cur, extra) {
          extra = extra || {};
          var hl = {};
          Object.keys(done).forEach(function (k) { hl[k] = 'ok'; });
          q.forEach(function (id) { hl[id] = 'cmp'; });
          if (cur != null) hl[cur] = 'cur';
          var oh = {}; if (extra.outNew) oh[out.length - 1] = 'new';
          var qh = {}; if (extra.qNew) qh[q.length - 1] = 'new';
          rec.step(line, msg, { hl: hl, queue: q.map(function (id) { return m.nodes[id].val; }), qHl: qh, out: out.slice(), outHl: oh });
        }
        q.push(m.root);
        st(1, '根节点 ' + m.nodes[m.root].val + ' 入队。', null, { qNew: true });
        while (q.length) {
          var id = q.shift(), n = m.nodes[id];
          out.push(n.val); done[id] = true;
          st(4, '队头 ' + n.val + ' 出队并访问。', id, { outNew: true });
          if (n.left != null) { q.push(n.left); st(5, n.val + ' 的左孩子 ' + m.nodes[n.left].val + ' 入队。', id, { qNew: true }); }
          if (n.right != null) { q.push(n.right); st(6, n.val + ' 的右孩子 ' + m.nodes[n.right].val + ' 入队。', id, { qNew: true }); }
        }
        st(2, '队列为空，遍历结束：' + out.join(' ') + '。先入队的节点（上层、左边）先被访问。', null);
      }
    }]
  });

  // ---------- 非递归中序遍历 ----------
  DSViz.register('tree-inorder-iter', {
    title: '非递归中序遍历：用栈代替调用栈',
    height: 330,
    code: [
      'TreeNode* cur = root;',
      'while (cur != NULL || top > 0) {',
      '    while (cur != NULL) {',
      '        stack[top++] = cur;',
      '        cur = cur->left;',
      '    }',
      '    cur = stack[--top];',
      '    printf("%c ", cur->value);',
      '    cur = cur->right;',
      '}'
    ],
    init: function () { return build(DEFAULT); },
    view: function (m, x) {
      return {
        kind: 'multi', vars: x.vars || {},
        parts: [treePart(m, x), vstack(x.stack || [], '栈', x.topHl), row(x.out || [], 290, '输出', x.outHl)]
      };
    },
    start: function (m, rec) { rec.step(0, '先一路向左把节点压栈；走到头之后弹出栈顶访问，再转向它的右子树。点击「开始」。'); },
    actions: [{
      label: '开始',
      fields: [descField()],
      check: checkDesc,
      run: function (m, p, rec) {
        load(m, p);
        var stack = [], out = [], done = {}, cur = m.root;
        function st(line, msg, extra) {
          extra = extra || {};
          var hl = {};
          Object.keys(done).forEach(function (k) { hl[k] = 'ok'; });
          stack.forEach(function (id) { hl[id] = 'cmp'; });
          if (cur != null) hl[cur] = 'cur';
          var oh = {}; if (extra.outNew) oh[out.length - 1] = 'new';
          rec.step(line, msg, { hl: hl, ptrs: cur != null ? [{ name: 'cur', at: cur }] : [], stack: stack.map(function (id) { return m.nodes[id].val; }), topHl: extra.topHl, out: out.slice(), outHl: oh, vars: { cur: cur == null ? 'NULL' : m.nodes[cur].val } });
        }
        st(1, 'cur 从根开始。');
        while (cur != null || stack.length) {
          while (cur != null) {
            stack.push(cur);
            st(4, m.nodes[cur].val + ' 入栈：它的左子树还没有处理，先记下它。', { topHl: 'new' });
            cur = m.nodes[cur].left;
            st(5, 'cur 走向左孩子' + (cur == null ? '，是 NULL，左边走到头了。' : ' ' + m.nodes[cur].val + '。'));
          }
          cur = stack.pop();
          out.push(m.nodes[cur].val); done[cur] = true;
          st(8, '弹出栈顶 ' + m.nodes[cur].val + '：它的左子树已经处理完了，访问它。', { outNew: true });
          cur = m.nodes[cur].right;
          st(9, 'cur 转向右子树' + (cur == null ? '（NULL）。' : ' ' + m.nodes[cur].val + '。'));
        }
        st(2, 'cur 为 NULL 且栈为空，结束：' + out.join(' ') + '，和递归版本的结果相同。');
      }
    }]
  });
})();
