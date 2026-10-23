/* 《哈希表》：链地址法和线性探测（为了便于观察，键是整数，哈希函数是 key % 表长） */
(function () {
  'use strict';

  function isInt(m, p) { return Number.isInteger(p.key) && p.key >= 0 && p.key < 1000 ? null : '请输入 0 到 999 之间的整数'; }

  // ---------- 链地址法 ----------
  var NB = 7;
  function chainInit() {
    var m = { buckets: [], nodes: {}, nextId: 100 };
    for (var b = 0; b < NB; b++) m.buckets.push([]);
    [15, 8, 23, 4, 11, 30].forEach(function (k) { var id = m.nextId++; m.nodes[id] = k; m.buckets[k % NB].unshift(id); });
    return m;
  }
  function chainView(m, x) {
    var nodes = [], links = [];
    for (var b = 0; b < NB; b++) {
      nodes.push({ id: b, val: '[' + b + ']', col: 0, row: b, hl: (x.bhl || {})[b] });
      var prev = b;
      m.buckets[b].forEach(function (id, j) {
        nodes.push({ id: id, val: m.nodes[id], col: j + 1, row: b, hl: (x.hl || {})[id] });
        links.push({ from: prev, to: id, hl: (x.lhl || {})[prev] });
        prev = id;
      });
    }
    return { kind: 'list', nodes: nodes, links: links, top: 24, rowH: 46, gap: 92, ptrs: [], vars: x.vars || {} };
  }

  DSViz.register('hash-chain', {
    title: '链地址法：7 个桶，hash(key) = key % 7',
    height: 350,
    code: [
      'size_t b = m->hash(key) % m->nbuckets;',
      'for (Entry* e = m->buckets[b]; e; e = e->next) {',
      '    if (strcmp(e->key, key) == 0) { e->value = value; return; }',
      '}',
      'Entry* e = malloc(sizeof(Entry));',
      '/* …… 复制 key，写入 value …… */',
      'e->next = m->buckets[b];',
      'm->buckets[b] = e;'
    ],
    init: chainInit,
    view: chainView,
    start: function (m, rec) { rec.step(0, '每个桶是一个链表，哈希值相同的键放在同一个链表中。15、8 都等于 1（mod 7），挂在同一个桶里。试试插入 22（也落在桶 1），或者查找 30。'); },
    actions: [{
      label: '插入',
      fields: [{ key: 'key', label: 'key', value: 22, size: 4 }],
      check: function (m, p) { return isInt(m, p) || (Object.keys(m.nodes).length >= 16 ? '演示最多 16 个元素' : null); },
      run: function (m, p, rec) {
        var b = p.key % NB, bh = {}; bh[b] = 'cur';
        rec.step(1, 'hash(' + p.key + ') = ' + p.key + ' % 7 = ' + b + '，去桶 ' + b + '。', { bhl: bh });
        var list = m.buckets[b];
        for (var j = 0; j < list.length; j++) {
          var h = {}; h[list[j]] = 'cmp';
          if (m.nodes[list[j]] === p.key) { h[list[j]] = 'warn'; rec.step(3, '链表中已经有 ' + p.key + '，不重复插入（如果是键值对，就更新它的值）。', { bhl: bh, hl: h }); return; }
          rec.step(3, '比较 ' + m.nodes[list[j]] + '，不是 ' + p.key + '。', { bhl: bh, hl: h });
        }
        var id = m.nextId++; m.nodes[id] = p.key; list.unshift(id);
        var hn = {}; hn[id] = 'new';
        rec.step(8, '没有找到，新建一个节点插在这个桶的链表头部（O(1)）。桶 ' + b + ' 现在有 ' + list.length + ' 个元素。', { bhl: bh, hl: hn });
      }
    }, {
      label: '查找',
      fields: [{ key: 'key', label: 'key', value: 30, size: 4 }],
      check: isInt,
      run: function (m, p, rec) {
        var b = p.key % NB, bh = {}, list = m.buckets[b];
        bh[b] = 'cur';
        rec.step(1, 'hash(' + p.key + ') = ' + b + '，只需要在桶 ' + b + ' 的链表中查找，其他的桶完全不用看。', { bhl: bh });
        for (var j = 0; j < list.length; j++) {
          var h = {}; h[list[j]] = m.nodes[list[j]] === p.key ? 'ok' : 'cmp';
          if (m.nodes[list[j]] === p.key) { rec.step(3, '找到了 ' + p.key + '，比较了 ' + (j + 1) + ' 次。', { bhl: bh, hl: h }); return; }
          rec.step(3, '比较 ' + m.nodes[list[j]] + '，不是。', { bhl: bh, hl: h });
        }
        rec.step(2, '链表走完了，没有 ' + p.key + '。查找的代价取决于链表的长度，平均是负载因子（元素数 / 桶数）。', { bhl: bh });
      }
    }, {
      label: '恢复',
      run: function (m, p, rec) { var f = chainInit(); m.buckets = f.buckets; m.nodes = f.nodes; m.nextId = f.nextId; rec.step(0, '已恢复。'); }
    }]
  });

  // ---------- 线性探测 ----------
  var CAP = 11;
  function probeInit() { var m = { t: [] }; for (var i = 0; i < CAP; i++) m.t.push(null); [12, 23, 5, 34].forEach(function (k) { var i = k % CAP; while (m.t[i] != null) i = (i + 1) % CAP; m.t[i] = k; }); return m; }
  function probeView(m, x) {
    var used = m.t.filter(function (v) { return v != null; }).length;
    return {
      kind: 'array', cellW: 56, x0: 52, top: 80, cells: m.t.map(function (v, i) { return { v: v == null ? '' : v, hl: (x.hl || {})[i] || (v == null ? 'empty' : null) }; }),
      ptrs: x.ptrs || [], label: '表长 11，hash(key) = key % 11；虚线格子是空位',
      vars: x.vars || { '元素': used, '负载因子': (used / CAP).toFixed(2) }
    };
  }

  DSViz.register('hash-probe', {
    title: '开放寻址（线性探测）：被占了就看下一个位置',
    height: 170,
    code: [
      'size_t i = hash(key) % s->cap;',
      'while (s->slots[i] != EMPTY) {',
      '    if (s->slots[i] == key) return 0;',
      '    i = (i + 1) % s->cap;',
      '}',
      's->slots[i] = key;'
    ],
    init: probeInit,
    view: probeView,
    start: function (m, rec) { rec.step(0, '12、23、34 的哈希值都是 1，它们依次占据了 1、2、3，形成了一个「聚集」。插入 45（哈希值也是 1）或 13（哈希值 2），看看要探测几次。'); },
    actions: [{
      label: '插入',
      fields: [{ key: 'key', label: 'key', value: 45, size: 4 }],
      check: function (m, p) { return isInt(m, p) || (m.t.every(function (v) { return v != null; }) ? '表满了' : null); },
      run: function (m, p, rec) {
        var i = p.key % CAP, probes = 1;
        var h = {}; h[i] = 'cur';
        rec.step(1, 'hash(' + p.key + ') = ' + p.key + ' % 11 = ' + i + '。', { hl: h, ptrs: [{ name: 'i', at: i }] });
        while (m.t[i] != null) {
          if (m.t[i] === p.key) { var w = {}; w[i] = 'warn'; rec.step(3, p.key + ' 已经在表中了。', { hl: w, ptrs: [{ name: 'i', at: i }] }); return; }
          var o = {}; o[i] = 'cmp';
          rec.step(4, '位置 ' + i + ' 已经被 ' + m.t[i] + ' 占了，看下一个位置。', { hl: o, ptrs: [{ name: 'i', at: i }] });
          i = (i + 1) % CAP; probes++;
        }
        m.t[i] = p.key;
        var n = {}; n[i] = 'new';
        rec.step(6, '位置 ' + i + ' 是空的，放入 ' + p.key + '。一共探测了 ' + probes + ' 次。聚集越长，落在聚集中的键要探测的次数就越多，而且聚集还会越变越长。', { hl: n, ptrs: [{ name: 'i', at: i }] });
      }
    }, {
      label: '查找',
      fields: [{ key: 'key', label: 'key', value: 56, size: 4 }],
      check: isInt,
      run: function (m, p, rec) {
        var i = p.key % CAP, probes = 0;
        rec.step(1, 'hash(' + p.key + ') = ' + i + '，从这里开始找。', { ptrs: [{ name: 'i', at: i }] });
        for (var c = 0; c < CAP; c++) {
          probes++;
          var o = {};
          if (m.t[i] == null) { rec.step(2, '位置 ' + i + ' 是空位：如果 ' + p.key + ' 存在，插入时一定会放在这里或之前，所以它不存在。探测了 ' + probes + ' 次。', { ptrs: [{ name: 'i', at: i }] }); return; }
          if (m.t[i] === p.key) { o[i] = 'ok'; rec.step(3, '找到了 ' + p.key + '，探测了 ' + probes + ' 次。', { hl: o, ptrs: [{ name: 'i', at: i }] }); return; }
          o[i] = 'cmp';
          rec.step(4, '位置 ' + i + ' 是 ' + m.t[i] + '，不是，继续。', { hl: o, ptrs: [{ name: 'i', at: i }] });
          i = (i + 1) % CAP;
        }
        rec.step(2, '找遍了整张表也没有。');
      }
    }, {
      label: '恢复',
      run: function (m, p, rec) { m.t = probeInit().t; rec.step(0, '已恢复。'); }
    }]
  });
})();
