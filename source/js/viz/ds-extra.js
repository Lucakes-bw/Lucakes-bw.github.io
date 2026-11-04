/* 数据结构补充篇的逐步演示
 *
 * rb-insert  红黑树：插入后的三种调整情况（红色边框 = 红色节点）
 * skiplist   跳表：从最高层开始查找，走不动就下降
 * trie       Trie：插入、查找、前缀计数
 * lru        LRU 缓存：哈希表 + 双向链表
 */
(function () {
  'use strict';

  function box(id, x, y, w, h, text, hl, sub) { return { id: id, x: x, y: y, w: w, h: h, text: text, hl: hl || null, sub: sub }; }
  function txt(id, x, y, text, cls, anchor) { return { id: id, x: x, y: y, text: text, cls: cls || null, anchor: anchor || 'start' }; }

  /* ---------- 红黑树插入 ---------- */

  function rbEmpty() { return { nodes: {}, root: null, nextId: 1 }; }
  function isRed(m, id) { return id != null && m.nodes[id].red; }

  function rbView(m, x) {
    var list = Object.keys(m.nodes).map(function (k) {
      var n = m.nodes[k];
      return { id: n.id, val: n.val, left: n.left, right: n.right, hl: n.red ? 'warn' : null };
    });
    var ptrs = [];
    Object.keys(x.ptrs || {}).forEach(function (name) { if (x.ptrs[name] != null) ptrs.push({ name: name, at: x.ptrs[name], hl: name === 'z' ? 'warn' : null }); });
    return { kind: 'tree', nodes: list, root: m.root, treeWidth: 700, top: 48, treeHeight: 290, maxGap: 46, maxDy: 54, ptrs: ptrs,
      vars: x.vars || { '节点数': list.length, '旋转次数': m.rot || 0 } };
  }

  function rotate(m, xId, dir) {          // dir = 'left'：x 的右孩子升上来
    var other = dir === 'left' ? 'right' : 'left';
    var x = m.nodes[xId], yId = x[other], y = m.nodes[yId];
    x[other] = y[dir];
    if (y[dir] != null) m.nodes[y[dir]].parent = xId;
    y.parent = x.parent;
    if (x.parent == null) m.root = yId;
    else if (m.nodes[x.parent].left === xId) m.nodes[x.parent].left = yId;
    else m.nodes[x.parent].right = yId;
    y[dir] = xId;
    x.parent = yId;
    m.rot = (m.rot || 0) + 1;
  }

  function rbInsert(m, key, rec) {
    function step(line, msg, ptrs) { if (rec) rec.step(line, msg, { ptrs: ptrs || {} }); }
    var p = null, cur = m.root;
    while (cur != null) {
      if (key === m.nodes[cur].val) { step(3, key + ' 已经在树中了。', { z: cur }); return; }
      p = cur;
      cur = key < m.nodes[cur].val ? m.nodes[cur].left : m.nodes[cur].right;
    }
    var z = m.nextId++;
    m.nodes[z] = { id: z, val: key, left: null, right: null, parent: p, red: true };
    if (p == null) m.root = z;
    else if (key < m.nodes[p].val) m.nodes[p].left = z;
    else m.nodes[p].right = z;
    var rotated = false;
    step(3, '按二叉搜索树的规则插入 ' + key + '，新节点涂成红色：不改变任何路径上的黑色节点数。', { z: z });
    while (isRed(m, m.nodes[z].parent)) {
      var pz = m.nodes[z].parent, g = m.nodes[pz].parent;
      var side = m.nodes[g].left === pz ? 'left' : 'right', oside = side === 'left' ? 'right' : 'left';
      var u = m.nodes[g][oside];
      step(4, '父亲 ' + m.nodes[pz].val + ' 也是红色，出现了两个相邻的红色节点，要调整。', { z: z, p: pz, g: g, u: u });
      if (isRed(m, u)) {
        m.nodes[pz].red = false; m.nodes[u].red = false; m.nodes[g].red = true;
        step(10, '情况 1：叔叔 ' + m.nodes[u].val + ' 是红色。父亲和叔叔涂黑，祖父 ' + m.nodes[g].val + ' 涂红：每条路径的黑色节点数不变。', { z: z, p: pz, g: g, u: u });
        z = g;
        step(11, '问题可能上移到了祖父：z 指向 ' + m.nodes[z].val + '，继续检查它和它的父亲。', { z: z });
        continue;
      }
      step(12, '叔叔' + (u == null ? '是 NIL（黑色）' : ' ' + m.nodes[u].val + ' 是黑色') + '，变色解决不了，要旋转。', { z: z, p: pz, g: g, u: u });
      if (m.nodes[pz][oside] === z) {
        z = pz;
        rotate(m, z, side);
        step(15, '情况 2：z 是内侧的孩子（和父亲不在同一边）。对父亲' + (side === 'left' ? '左' : '右') + '旋，变成情况 3。', { z: z, p: m.nodes[z].parent, g: g });
        pz = m.nodes[z].parent;
      }
      m.nodes[pz].red = false; m.nodes[g].red = true;
      step(18, '情况 3：父亲 ' + m.nodes[pz].val + ' 涂黑，祖父 ' + m.nodes[g].val + ' 涂红。', { z: z, p: pz, g: g });
      rotate(m, g, oside);
      rotated = true;
      step(19, '对祖父' + (oside === 'left' ? '左' : '右') + '旋：黑色的 ' + m.nodes[pz].val + ' 成为这棵子树的根，两个孩子是红色。调整结束。', { z: z, p: pz });
    }
    if (m.nodes[m.root].red) {
      m.nodes[m.root].red = false;
      step(22, '根被涂成了红色，涂回黑色：所有路径的黑色节点数同时加一，不破坏性质。', { z: m.root });
    } else if (!rotated) {
      step(22, '父亲是黑色（或者 z 就是根），没有相邻的红色节点，插入完成。', { z: z });
    }
  }

  DSViz.register('rb-insert', {
    title: '红黑树：插入和调整（红色边框 = 红色节点）',
    height: 320,
    code: [
      'void rbInsert(RBTree* t, int key)',
      '{',
      '    /* 按二叉搜索树找位置，新节点 z 涂成红色 */',
      '    while (z->parent->color == RED) {',
      '        RBNode* g = z->parent->parent;',
      '        RBNode* uncle = 父亲的兄弟;',
      '        if (uncle->color == RED) {        // 情况 1',
      '            z->parent->color = BLACK;',
      '            uncle->color = BLACK;',
      '            g->color = RED;',
      '            z = g;',
      '        } else {',
      '            if (z 是内侧的孩子) {          // 情况 2',
      '                z = z->parent;',
      '                旋转 z，让它变成外侧的孩子;',
      '            }',
      '            z->parent->color = BLACK;     // 情况 3',
      '            g->color = RED;',
      '            反方向旋转 g;',
      '        }',
      '    }',
      '    t->root->color = BLACK;',
      '}'
    ],
    init: function () {
      var m = rbEmpty();
      [10, 20, 30, 15].forEach(function (k) { rbInsert(m, k, null); });
      m.rot = 0;
      return m;
    },
    view: rbView,
    actions: [
      { label: '插入', fields: [{ key: 'v', label: 'key', value: 25 }],
        check: function (m, p) {
          if (typeof p.v !== 'number' || p.v !== Math.floor(p.v) || p.v < 0 || p.v > 99) return '请输入 0～99 的整数';
          if (Object.keys(m.nodes).length >= 15) return '节点太多了，先清空';
        },
        run: function (m, p, rec) { rbInsert(m, p.v, rec); } },
      { label: '有序插入 1～10',
        run: function (m, p, rec) {
          var e = rbEmpty(); m.nodes = e.nodes; m.root = null; m.nextId = 1; m.rot = 0;
          rec.step(3, '从空树开始，依次插入 1 到 10。普通的二叉搜索树会退化成一条链。', { ptrs: {} });
          for (var k = 1; k <= 10; k++) rbInsert(m, k, rec);
          rec.step(22, '插入完成：高度 5，一共旋转 ' + m.rot + ' 次。', { ptrs: {} });
        } },
      { label: '清空',
        run: function (m, p, rec) {
          m.nodes = {}; m.root = null; m.nextId = 1; m.rot = 0;
          rec.step(1, '树已清空。', { ptrs: {} });
        } }
    ]
  });

  /* ---------- 跳表查找 ---------- */

  var SL_KEYS = [3, 7, 12, 17, 19, 21, 25, 30], SL_LV = [1, 3, 1, 2, 1, 4, 1, 2], SL_TOP = 4;
  var SLW = 52, SLH = 26, SLX = 24, SLG = 70, SLY = 40, SLDY = 44;

  function slBox(col, lv) { return 'n' + col + '-' + lv; }   // col 0 是头节点，col n+1 是 NIL

  function slView(m, x) {
    var boxes = [], arrows = [], texts = [], hl = x.hl || {};
    var n = SL_KEYS.length;
    for (var lv = SL_TOP - 1; lv >= 0; lv--) {
      var y = SLY + (SL_TOP - 1 - lv) * SLDY;
      texts.push(txt('lv' + lv, SLX - 6, y + SLH / 2 + 4, 'L' + lv, 'muted', 'end'));
      boxes.push(box(slBox(0, lv), SLX, y, SLW, SLH, 'head', hl[slBox(0, lv)]));
      for (var i = 0; i < n; i++) if (SL_LV[i] > lv) boxes.push(box(slBox(i + 1, lv), SLX + (i + 1) * SLG, y, SLW, SLH, String(SL_KEYS[i]), hl[slBox(i + 1, lv)]));
      boxes.push(box(slBox(n + 1, lv), SLX + (n + 1) * SLG, y, SLW - 10, SLH, 'NIL', 'dim'));
      var from = 0;
      for (var j = 1; j <= n + 1; j++) {
        if (j === n + 1 || SL_LV[j - 1] > lv) {
          arrows.push({ from: slBox(from, lv), to: slBox(j, lv), hl: (x.ahl || {})[slBox(from, lv)] });
          from = j;
        }
      }
    }
    if (x.note) texts.push(txt('note', SLX, SLY + SL_TOP * SLDY + 14, x.note, 'muted'));
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts, vars: x.vars || {} };
  }

  function slNext(col, lv) {             // col 所在节点在第 lv 层的下一个节点
    for (var j = col + 1; j <= SL_KEYS.length; j++) if (SL_LV[j - 1] > lv) return j;
    return SL_KEYS.length + 1;
  }

  function slSearch(m, p, rec) {
    var key = p.k, x = 0, cmp = 0, hl = {}, ahl = {};
    function keyOf(c) { return c > SL_KEYS.length ? 'NIL' : String(SL_KEYS[c - 1]); }
    function snap(line, msg, extra) { rec.step(line, msg, Object.assign({ hl: Object.assign({}, hl), ahl: Object.assign({}, ahl), vars: { key: key, '比较次数': cmp } }, extra || {})); }
    hl[slBox(0, SL_TOP - 1)] = 'cur';
    snap(1, '查找 ' + key + '：从头节点的最高层 L' + (SL_TOP - 1) + ' 开始。');
    for (var lv = SL_TOP - 1; lv >= 0; lv--) {
      for (;;) {
        var nx = slNext(x, lv), nk = keyOf(nx);
        var stop = nx > SL_KEYS.length || SL_KEYS[nx - 1] >= key;
        if (nx <= SL_KEYS.length) cmp++;
        hl[slBox(nx, lv)] = 'cmp';
        snap(3, 'L' + lv + '：下一个是 ' + nk + (stop ? (nk === 'NIL' ? '，到头了' : ' ≥ ' + key + '，不能再往右') : ' < ' + key + '，往右走') + '。');
        delete hl[slBox(nx, lv)];
        if (nx === SL_KEYS.length + 1) hl[slBox(nx, lv)] = 'dim';
        if (stop) break;
        hl[slBox(x, lv)] = 'dim';
        ahl[slBox(x, lv)] = 'ok';
        x = nx;
        hl[slBox(x, lv)] = 'cur';
        snap(4, '走到 ' + keyOf(x) + '。');
      }
      if (lv > 0) {
        hl[slBox(x, lv)] = 'dim';
        hl[slBox(x, lv - 1)] = 'cur';
        snap(5, '在 ' + (x === 0 ? 'head' : keyOf(x)) + ' 处下降到 L' + (lv - 1) + '。');
      }
    }
    var r = slNext(x, 0), found = r <= SL_KEYS.length && SL_KEYS[r - 1] === key;
    hl[slBox(x, 0)] = 'dim';
    hl[slBox(r, 0)] = found ? 'ok' : 'warn';
    snap(7, found ? '找到了 ' + key + '，一共比较了 ' + cmp + ' 次。底层的有序链表从头走要比较 ' + (SL_KEYS.indexOf(key) + 1) + ' 次。'
      : key + ' 不存在：底层中它应该在 ' + (x === 0 ? 'head' : keyOf(x)) + ' 和 ' + keyOf(r) + ' 之间。一共比较了 ' + cmp + ' 次。');
  }

  DSViz.register('skiplist', {
    title: '跳表：从最高层开始，尽量往右，走不动就下降',
    height: 250,
    width: 720,
    code: [
      'SLNode* x = sl->head;',
      'for (int i = sl->level - 1; i >= 0; --i) {',
      '    while (x->next[i] && x->next[i]->key < key)',
      '        x = x->next[i];               // 向右',
      '}                                     // 走不动，下降一层',
      'x = x->next[0];',
      'return x && x->key == key ? x : NULL;'
    ],
    init: function () { return {}; },
    view: slView,
    start: function (m, rec) { slSearch(m, { k: 25 }, rec); },
    actions: [{ label: '查找', fields: [{ key: 'k', label: 'key', value: 25 }],
      check: function (m, p) { if (typeof p.k !== 'number' || p.k < 0 || p.k > 99) return '请输入 0～99 的数'; },
      run: slSearch }]
  });

  /* ---------- Trie ---------- */

  function trieEmpty() { return { nodes: { '^': { pass: 0, end: false, kids: {} } } }; }

  function trieView(m, x) {
    var hl = x.hl || {}, ehl = x.ehl || {}, nodes = [], edges = [], leaf = 0, pos = {};
    (function place(id, d) {
      var n = m.nodes[id], ks = Object.keys(n.kids).sort();
      if (!ks.length) pos[id] = { x: leaf++, d: d };
      else {
        ks.forEach(function (c) { place(id + c, d + 1); });
        pos[id] = { x: (pos[id + ks[0]].x + pos[id + ks[ks.length - 1]].x) / 2, d: d };
      }
    })('^', 0);
    var gap = Math.min(96, 640 / Math.max(leaf, 1)), x0 = 360 - gap * (leaf - 1) / 2;
    Object.keys(m.nodes).forEach(function (id) {
      var n = m.nodes[id], p = pos[id];
      nodes.push({ id: id, label: id === '^' ? 'root' : String(n.pass), x: x0 + p.x * gap, y: 36 + p.d * 62,
        hl: hl[id] || (n.end ? 'ok' : null), note: n.end && !Object.keys(n.kids).length ? id.slice(1) : null });
      Object.keys(n.kids).forEach(function (c) { edges.push({ a: id, b: id + c, w: c, directed: true, hl: ehl[id + c] }); });
    });
    return { kind: 'graph', nodes: nodes, edges: edges, vars: x.vars || {} };
  }

  function trieInsert(m, w, rec) {
    var id = '^', hl = {}, ehl = {};
    function snap(line, msg) { if (rec) rec.step(line, msg, { hl: Object.assign({}, hl), ehl: Object.assign({}, ehl), vars: { word: w } }); }
    for (var i = 0; i < w.length; i++) {
      var c = w[i], nid = id + c, made = !m.nodes[id].kids[c];
      if (made) { m.nodes[id].kids[c] = true; m.nodes[nid] = { pass: 0, end: false, kids: {} }; }
      hl[id] = 'dim'; hl[nid] = made ? 'new' : 'cur'; ehl[nid] = 'ok';
      snap(made ? 5 : 6, (made ? '没有字符 ' + c + ' 这条边，新建一个节点' : '字符 ' + c + ' 的边已经有了，直接走下去') + '：前缀 ' + nid.slice(1) + '。');
      id = nid;
    }
    if (m.nodes[id].end) { hl[id] = 'warn'; snap(8, w + ' 已经在 Trie 中了，不重复计数。'); return; }
    m.nodes[id].end = true;
    var q = '^';
    for (;;) { m.nodes[q].pass++; if (q.length - 1 === w.length) break; q = q + w[q.length - 1]; }
    hl[id] = 'ok';
    snap(10, '在 ' + w + ' 的节点上标记 end，再沿路径把 pass 加一（节点中的数字就是 pass：以这个前缀开头的单词数）。');
  }

  function trieWalk(m, s, rec, mode) {
    var id = '^', hl = {}, ehl = {};
    var vars = {}; vars[mode === 'find' ? 'word' : 'prefix'] = s;
    function snap(line, msg) { rec.step(line, msg, { hl: Object.assign({}, hl), ehl: Object.assign({}, ehl), vars: vars }); }
    hl[id] = 'cur';
    snap(13, '从根开始，沿着「' + s + '」的字符往下走。');
    for (var i = 0; i < s.length; i++) {
      var c = s[i];
      if (!m.nodes[id].kids[c]) {
        hl[id] = 'warn';
        snap(14, '前缀 ' + (id.slice(1) || '（空）') + ' 下面没有字符 ' + c + ' 这条边：' + (mode === 'find' ? s + ' 不存在。' : '以 ' + s + ' 开头的单词有 0 个。'));
        return;
      }
      hl[id] = 'dim'; id += c; hl[id] = 'cur'; ehl[id] = 'ok';
      snap(14, '走字符 ' + c + '，到达前缀 ' + id.slice(1) + '。');
    }
    var n = m.nodes[id];
    if (mode === 'find') {
      hl[id] = n.end ? 'ok' : 'warn';
      snap(16, n.end ? '路径存在，而且节点上有 end 标记：' + s + ' 在 Trie 中。' : '路径存在，但这里没有单词结束（' + s + ' 只是别的单词的前缀）：不在 Trie 中。');
    } else {
      hl[id] = 'ok';
      snap(17, '节点的 pass 是 ' + n.pass + '：以 ' + s + ' 开头的单词有 ' + n.pass + ' 个，不用遍历子树。');
    }
  }

  var TRIE_WORDS = ['tea', 'ten', 'to', 'inn', 'in', 'a'];
  function trieCheck(m, p) {
    var s = String(p.w);
    if (!/^[a-z]{1,5}$/.test(s)) return '请输入 1～5 个小写字母';
    if (Object.keys(m.nodes).length > 22) return '节点太多了，先重建';
  }

  DSViz.register('trie', {
    title: 'Trie：插入、查找和前缀计数（绿色 = 有单词在这里结束）',
    height: 300,
    code: [
      'bool trieInsert(TrieNode* root, const char* w)',
      '{',
      '    TrieNode* x = root;',
      '    for (const char* p = w; *p; ++p) {',
      '        if (!x->child[*p - \'a\']) 新建节点;',
      '        x = x->child[*p - \'a\'];',
      '    }',
      '    if (x->end) return false;',
      '    x->end = true;',
      '    沿路径 pass++;',
      '}',
      '',
      'TrieNode* x = root;                  // 查找、前缀计数',
      'for (每个字符 c) x = x->child[c];     // 走不通就是 NULL',
      '',
      'contains = x && x->end;',
      'countPrefix = x ? x->pass : 0;'
    ],
    init: trieEmpty,
    view: trieView,
    start: function (m, rec) {
      rec.step(1, '依次插入 ' + TRIE_WORDS.join('、') + '。', { vars: {} });
      TRIE_WORDS.forEach(function (w) { trieInsert(m, w, rec); });
    },
    actions: [
      { label: '查找', fields: [{ key: 'w', label: '单词', value: 'in', size: 6 }], check: trieCheck,
        run: function (m, p, rec) { trieWalk(m, String(p.w), rec, 'find'); } },
      { label: '前缀计数', fields: [{ key: 'w', label: '前缀', value: 'te', size: 6 }], check: trieCheck,
        run: function (m, p, rec) { trieWalk(m, String(p.w), rec, 'prefix'); } },
      { label: '插入', fields: [{ key: 'w', label: '单词', value: 'tent', size: 6 }], check: trieCheck,
        run: function (m, p, rec) { trieInsert(m, String(p.w), rec); } },
      { label: '重建',
        run: function (m, p, rec) {
          m.nodes = trieEmpty().nodes;
          rec.step(1, '依次插入 ' + TRIE_WORDS.join('、') + '。', { vars: {} });
          TRIE_WORDS.forEach(function (w) { trieInsert(m, w, rec); });
        } }
    ]
  });

  /* ---------- LRU 缓存 ---------- */

  var LW = 76, LG = 120, LX = 20, LY = 30;

  function lruView(m, x) {
    var boxes = [], arrows = [], texts = [], hl = x.hl || {};
    var ids = ['H'].concat(m.list.map(function (e) { return 'k' + e.k; })).concat(['T']);
    ids.forEach(function (id, i) {
      var e = m.list[i - 1];
      var label = id === 'H' ? 'head' : id === 'T' ? 'tail' : e.k + ':' + e.v;
      var sub = id === 'H' ? '最近用过 →' : id === 'T' ? '← 最久没用' : null;
      boxes.push(box(id, LX + i * LG, LY, id === 'H' || id === 'T' ? 60 : LW, 36, label, hl[id] || (id === 'H' || id === 'T' ? 'dim' : null), sub));
      if (i > 0) {
        arrows.push({ from: ids[i - 1], to: id, curve: -9 });
        arrows.push({ from: id, to: ids[i - 1], curve: 9 });
      }
    });
    if (x.gone) {
      boxes.push(box('k' + x.gone.k, LX + (m.list.length + 1) * LG, LY + 64, LW, 36, x.gone.k + ':' + x.gone.v, 'del', '被淘汰'));
    }
    var keys = m.list.map(function (e) { return e.k; }).sort(function (a, b) { return a - b; });
    texts.push(txt('hash', LX, LY + 132, '哈希表：' + (keys.length ? keys.map(function (k) { return k + ' → 节点'; }).join('，') : '（空）'), 'muted'));
    texts.push(txt('cap', LX, LY + 154, '容量 ' + m.cap + '，当前 ' + m.list.length + ' 项', 'muted'));
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts, vars: x.vars || {} };
  }

  function lruGet(m, k, rec) {
    var i = m.list.findIndex(function (e) { return e.k === k; });
    m.ops++;
    if (i >= 0) m.hits++;
    var vars = { op: 'get(' + k + ')', '命中': m.hits + ' / ' + m.ops };
    if (i < 0) {
      rec.step(3, '哈希表中没有 ' + k + '：未命中，返回 -1。', { vars: vars });
      return;
    }
    var hl = {}; hl['k' + k] = 'cmp';
    rec.step(3, '哈希表中找到了 ' + k + '，直接拿到它在链表中的节点：O(1)。', { hl: hl, vars: vars });
    var e = m.list.splice(i, 1)[0];
    m.list.unshift(e);
    hl['k' + k] = 'ok';
    rec.step(5, '把它从链表中摘下（双向链表，O(1)），放到头部：现在它是最近用过的。返回 ' + e.v + '。', { hl: hl, vars: vars });
  }

  function lruPut(m, k, v, rec) {
    var i = m.list.findIndex(function (e) { return e.k === k; }), hl = {}, vars = { op: 'put(' + k + ', ' + v + ')' };
    if (i >= 0) {
      var e = m.list.splice(i, 1)[0];
      e.v = v;
      m.list.unshift(e);
      hl['k' + k] = 'ok';
      rec.step(12, k + ' 已经在缓存中：更新值，移到头部。', { hl: hl, vars: vars });
      return;
    }
    if (m.list.length === m.cap) {
      var old = m.list[m.list.length - 1];
      hl['k' + old.k] = 'warn';
      rec.step(15, '缓存满了。tail 的前一个 ' + old.k + ' 是最久没用过的，淘汰它。', { hl: hl, vars: vars });
      m.list.pop();
      rec.step(16, '把 ' + old.k + ' 从链表和哈希表中删掉（节点中存着 key，才能找到哈希表中的那一项）。', { gone: old, vars: vars });
      hl = {};
    }
    m.list.unshift({ k: k, v: v });
    hl['k' + k] = 'new';
    rec.step(18, '新建节点 ' + k + ':' + v + '，加入哈希表，放到链表头部。', { hl: hl, vars: vars });
  }

  function lruDemo(m, p, rec) {
    m.list = []; m.hits = 0; m.ops = 0;
    rec.step(1, '容量为 3 的 LRU 缓存。链表越靠左越是最近用过的。', { vars: {} });
    lruPut(m, 1, 10, rec); lruPut(m, 2, 20, rec); lruPut(m, 3, 30, rec);
    lruGet(m, 1, rec);
    lruPut(m, 4, 40, rec);
    lruGet(m, 2, rec);
  }

  function lruCheck(m, p) { if (typeof p.k !== 'number' || p.k !== Math.floor(p.k) || p.k < 0 || p.k > 99) return '请输入 0～99 的整数'; }

  DSViz.register('lru', {
    title: 'LRU 缓存：哈希表找节点，双向链表维护顺序',
    height: 200,
    width: 640,
    code: [
      'bool lruGet(LRUCache* c, int key, int* value)',
      '{',
      '    Entry* e = hfind(c, key);         // 哈希表',
      '    if (!e) return false;',
      '    unlink(e); pushFront(c, e);       // 移到头部',
      '    *value = e->value;',
      '    return true;',
      '}',
      '',
      'void lruPut(LRUCache* c, int key, int value)',
      '{',
      '    if (已经存在) { 更新值; 移到头部; return; }',
      '    if (c->size == c->capacity) {',
      '        Entry* old = c->tail.prev;    // 最久没用的',
      '        unlink(old);',
      '        hremove(c, old);',
      '    }',
      '    新建节点，加入哈希表，pushFront;',
      '}'
    ],
    init: function () { return { cap: 3, list: [], hits: 0, ops: 0 }; },
    view: lruView,
    start: function (m, rec) { lruDemo(m, {}, rec); },
    actions: [
      { label: 'get', fields: [{ key: 'k', label: 'key', value: 3 }], check: lruCheck,
        run: function (m, p, rec) { lruGet(m, p.k, rec); } },
      { label: 'put', fields: [{ key: 'k', label: 'key', value: 5 }, { key: 'v', label: 'value', value: 50 }], check: lruCheck,
        run: function (m, p, rec) { lruPut(m, p.k, typeof p.v === 'number' ? p.v : 0, rec); } },
      { label: '重新演示', run: lruDemo }
    ]
  });
})();
