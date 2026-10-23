/* 《AVL 树》：插入后沿路径向上检查平衡因子，失衡时旋转 */
(function () {
  'use strict';

  function empty() { return { nodes: {}, root: null, nextId: 1 }; }
  function H(m, id) { return id == null ? 0 : m.nodes[id].h; }
  function upd(m, id) { var n = m.nodes[id]; n.h = Math.max(H(m, n.left), H(m, n.right)) + 1; }
  function bf(m, id) { var n = m.nodes[id]; return H(m, n.left) - H(m, n.right); }
  function fmt(b) { return 'bf=' + (b > 0 ? '+' + b : b); }

  function view(m, x) {
    var list = Object.keys(m.nodes).map(function (k) {
      var n = m.nodes[k];
      return { id: n.id, val: n.val, left: n.left, right: n.right, hl: (x.hl || {})[n.id], sub: x.noBf ? null : fmt(bf(m, n.id)) };
    });
    return { kind: 'tree', nodes: list, root: m.root, treeWidth: 700, top: 40, treeHeight: 270, maxGap: 50, maxDy: 58, ptrs: x.ptrs || [], vars: x.vars || { '节点数': list.length, '树高': H(m, m.root) } };
  }
  function one(id, c) { var o = {}; if (id != null) o[id] = c; return o; }

  function setLink(m, parent, side, id) { if (parent == null) m.root = id; else m.nodes[parent][side] = id; }

  function rotateRight(m, y) { var x = m.nodes[y].left; m.nodes[y].left = m.nodes[x].right; m.nodes[x].right = y; upd(m, y); upd(m, x); return x; }
  function rotateLeft(m, x) { var y = m.nodes[x].right; m.nodes[x].right = m.nodes[y].left; m.nodes[y].left = x; upd(m, x); upd(m, y); return y; }

  function insert(m, key, rec) {
    var path = {};
    function ins(id, parent, side) {
      if (id == null) {
        var nid = m.nextId++;
        m.nodes[nid] = { id: nid, val: key, left: null, right: null, h: 1 };
        setLink(m, parent, side, nid);
        rec.step(20, '找到空位置，' + key + ' 作为叶子插入（和普通二叉搜索树一样）。接下来沿着刚才的路径往回走，逐个检查平衡因子。', { hl: one(nid, 'new') });
        return nid;
      }
      var n = m.nodes[id];
      if (key === n.val) { rec.step(23, key + ' 已经存在。', { hl: one(id, 'warn') }); return null; }
      var dir = key < n.val ? 'left' : 'right';
      path[id] = 'dim';
      rec.step(dir === 'left' ? 21 : 22, key + (dir === 'left' ? ' < ' : ' > ') + n.val + '，去' + (dir === 'left' ? '左' : '右') + '子树。', { hl: Object.assign({}, path, one(id, 'cmp')) });
      var r = ins(n[dir], id, dir);
      if (r == null) return null;
      delete path[id];
      return rebalance(id, parent, side);
    }
    function rebalance(id, parent, side) {
      upd(m, id);
      var b = bf(m, id), v = m.nodes[id].val;
      if (Math.abs(b) <= 1) {
        rec.step(15, '回到 ' + v + '：更新高度，平衡因子 ' + fmt(b) + '，在 -1 到 +1 之间，不需要调整。', { hl: Object.assign({}, path, one(id, 'cur')) });
        return id;
      }
      rec.step(4, '回到 ' + v + '：平衡因子 ' + fmt(b) + '，' + (b > 0 ? '左' : '右') + '边比' + (b > 0 ? '右' : '左') + '边高了 2，失衡了！', { hl: Object.assign({}, path, one(id, 'warn')) });
      var newRoot;
      if (b > 1) {
        var l = m.nodes[id].left;
        if (bf(m, l) < 0) {
          var lr = m.nodes[l].right;
          rec.step(6, 'LR 型：新节点在 ' + v + ' 的左孩子 ' + m.nodes[l].val + ' 的右子树中。先对 ' + m.nodes[l].val + ' 左旋，变成 LL 型。', { hl: Object.assign(one(id, 'warn'), one(l, 'cmp'), one(lr, 'cur')) });
          m.nodes[id].left = rotateLeft(m, l);
          rec.step(7, '左旋完成：' + m.nodes[lr].val + ' 升了上来。现在是 LL 型。', { hl: Object.assign(one(id, 'warn'), one(lr, 'cmp')) });
        } else {
          rec.step(5, 'LL 型：新节点在 ' + v + ' 的左孩子的左子树中。', { hl: Object.assign(one(id, 'warn'), one(l, 'cmp')) });
        }
        var up = m.nodes[id].left;
        newRoot = rotateRight(m, id);
        setLink(m, parent, side, newRoot);
        rec.step(8, '对 ' + v + ' 右旋：左孩子 ' + m.nodes[up].val + ' 升为这棵子树的根，' + v + ' 成为它的右孩子，' + m.nodes[up].val + ' 原来的右子树改挂到 ' + v + ' 的左边。中序顺序不变。', { hl: Object.assign(one(newRoot, 'ok'), one(id, 'cur')) });
      } else {
        var r = m.nodes[id].right;
        if (bf(m, r) > 0) {
          var rl = m.nodes[r].left;
          rec.step(11, 'RL 型：新节点在 ' + v + ' 的右孩子 ' + m.nodes[r].val + ' 的左子树中。先对 ' + m.nodes[r].val + ' 右旋，变成 RR 型。', { hl: Object.assign(one(id, 'warn'), one(r, 'cmp'), one(rl, 'cur')) });
          m.nodes[id].right = rotateRight(m, r);
          rec.step(12, '右旋完成：' + m.nodes[rl].val + ' 升了上来。现在是 RR 型。', { hl: Object.assign(one(id, 'warn'), one(rl, 'cmp')) });
        } else {
          rec.step(10, 'RR 型：新节点在 ' + v + ' 的右孩子的右子树中。', { hl: Object.assign(one(id, 'warn'), one(r, 'cmp')) });
        }
        var up2 = m.nodes[id].right;
        newRoot = rotateLeft(m, id);
        setLink(m, parent, side, newRoot);
        rec.step(13, '对 ' + v + ' 左旋：右孩子 ' + m.nodes[up2].val + ' 升为这棵子树的根，' + v + ' 成为它的左孩子，' + m.nodes[up2].val + ' 原来的左子树改挂到 ' + v + ' 的右边。', { hl: Object.assign(one(newRoot, 'ok'), one(id, 'cur')) });
      }
      return newRoot;
    }
    if (m.root == null) {
      var nid = m.nextId++;
      m.nodes[nid] = { id: nid, val: key, left: null, right: null, h: 1 };
      m.root = nid;
      rec.step(20, '树是空的，' + key + ' 成为根。', { hl: one(nid, 'new') });
      return;
    }
    var res = ins(m.root, null, null);
    if (res != null) rec.step(24, '插入完成，树高 ' + H(m, m.root) + '。所有节点的平衡因子都在 -1 到 +1 之间。');
  }

  DSViz.register('avl-insert', {
    title: 'AVL 树的插入与旋转（节点下方是平衡因子）',
    height: 300,
    code: [
      'static AVLNode* rebalance(AVLNode* n)',
      '{',
      '    update(n);',
      '    int b = balance(n);',
      '    if (b > 1) {',
      '        if (balance(n->left) < 0)',
      '            n->left = rotateLeft(n->left);    /* LR */',
      '        return rotateRight(n);               /* LL */',
      '    }',
      '    if (b < -1) {',
      '        if (balance(n->right) > 0)',
      '            n->right = rotateRight(n->right); /* RL */',
      '        return rotateLeft(n);                /* RR */',
      '    }',
      '    return n;',
      '}',
      '',
      'AVLNode* insert(AVLNode* root, int key)',
      '{',
      '    if (root == NULL) return newNode(key);',
      '    if (key < root->key) root->left = insert(root->left, key);',
      '    else if (key > root->key) root->right = insert(root->right, key);',
      '    else return root;',
      '    return rebalance(root);',
      '}'
    ],
    init: function () { return empty(); },
    view: view,
    start: function (m, rec) { rec.step(0, '先点击「有序插入 1..7」，观察每次失衡时的旋转；也可以逐个插入，试试 30、10、20 这样的 LR 型。', { noBf: true }); },
    actions: [{
      label: '插入',
      fields: [{ key: 'key', label: 'key', value: 30, size: 4 }],
      check: function (m, p) {
        if (!Number.isInteger(p.key) || Math.abs(p.key) > 999) return '请输入 -999 到 999 之间的整数';
        return Object.keys(m.nodes).length >= 15 ? '演示最多 15 个节点，先清空吧' : null;
      },
      run: function (m, p, rec) { insert(m, p.key, rec); }
    }, {
      label: '有序插入 1..7',
      run: function (m, p, rec) {
        Object.assign(m, empty());
        for (var k = 1; k <= 7; k++) insert(m, k, rec);
        rec.step(0, '1 到 7 按顺序插入，普通二叉搜索树会退化成高度为 7 的链表；AVL 树经过 4 次旋转，得到了高度为 3 的满二叉树。');
      }
    }, {
      label: '清空',
      run: function (m, p, rec) { Object.assign(m, empty()); rec.step(0, '树已清空。', { noBf: true }); }
    }]
  });
})();
