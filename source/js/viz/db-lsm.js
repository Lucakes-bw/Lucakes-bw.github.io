/* 《存储引擎》一章的逐步演示
 *
 * lsm-flow   LSM 树：写入进内存表，写满落盘成 L0 的文件，L0 攒够了和 L1 合并
 * lsm-bloom  布隆过滤器：加入时置 3 位，查询时只要有一位是 0 就一定没有
 */
(function () {
  'use strict';

  function box(id, x, y, w, h, text, hl, sub) { return { id: id, x: x, y: y, w: w, h: h, text: text, hl: hl || null, sub: sub }; }
  function txt(id, x, y, text, cls, anchor) { return { id: id, x: x, y: y, text: text, cls: cls || null, anchor: anchor || 'start' }; }

  /* ---------- LSM 树的写入、落盘与合并 ---------- */

  var MEM_CAP = 3, L0_TRIGGER = 2, FILE_CAP = 4;   // 演示用的很小的容量

  function flowInit() { return { mem: [], l0: [], l1: [], seq: 0 }; }

  function fmt(e) { return e.k + (e.del ? '✕' : '=' + e.v); }
  function sortEntries(a) { a.sort(function (x, y) { return x.k < y.k ? -1 : (x.k > y.k ? 1 : 0); }); }

  function flowView(m, x) {
    var boxes = [], texts = [], hl = x.hl || {};
    texts.push(txt('t0', 20, 20, '内存表（跳表，有序；最多 ' + MEM_CAP + ' 条）', 'muted'));
    if (!m.mem.length) boxes.push(box('mem', 20, 30, 120, 34, '（空）', 'dim'));
    m.mem.forEach(function (e, i) { boxes.push(box('m' + i, 20 + i * 90, 30, 84, 34, fmt(e), hl['m' + e.k] || null)); });
    texts.push(txt('t1', 20, 92, 'L0（文件之间键的范围会重叠；新的在右边；' + L0_TRIGGER + ' 个就合并）', 'muted'));
    if (!m.l0.length) boxes.push(box('l0e', 20, 102, 120, 34, '（空）', 'dim'));
    m.l0.forEach(function (f, i) { boxes.push(box('f0_' + i, 20 + i * 200, 102, 190, 34, f.map(fmt).join(' '), hl['f0_' + i] || 'cur')); });
    texts.push(txt('t2', 20, 164, 'L1（文件之间不重叠，按键排列；每个文件最多 ' + FILE_CAP + ' 条）', 'muted'));
    if (!m.l1.length) boxes.push(box('l1e', 20, 174, 120, 34, '（空）', 'dim'));
    m.l1.forEach(function (f, i) { boxes.push(box('f1_' + i, 20 + i * 200, 174, 190, 34, f.map(fmt).join(' '), hl['f1_' + i] || 'ok')); });
    if (x.note) texts.push(txt('n', 20, 236, x.note, 'muted'));
    return { kind: 'free', boxes: boxes, texts: texts, vars: x.vars || {} };
  }

  function compact(m, rec) {
    // 来源从新到旧：L0 倒序，然后 L1
    var sources = m.l0.slice().reverse().concat([[].concat.apply([], m.l1)]);
    var seen = {}, out = [];
    sources.forEach(function (src) {
      src.forEach(function (e) { if (!seen[e.k]) { seen[e.k] = true; out.push(e); } });
    });
    var dropped = out.filter(function (e) { return e.del; }).length;
    out = out.filter(function (e) { return !e.del; });   // L1 是最底层，墓碑可以丢掉
    sortEntries(out);
    var hl = {};
    m.l0.forEach(function (f, i) { hl['f0_' + i] = 'warn'; });
    m.l1.forEach(function (f, i) { hl['f1_' + i] = 'warn'; });
    rec.step(4, 'L0 有 ' + m.l0.length + ' 个文件了：把它们和整个 L1 多路归并。同一个键只留最新的版本，L1 是最底层，墓碑也一起丢掉。', { hl: hl });
    m.l1 = [];
    for (var i = 0; i < out.length; i += FILE_CAP) m.l1.push(out.slice(i, i + FILE_CAP));
    m.l0 = [];
    var h2 = {};
    m.l1.forEach(function (f, j) { h2['f1_' + j] = 'new'; });
    rec.step(5, '合并完成：L1 中每个键只有一个版本' + (dropped ? '，丢掉了 ' + dropped + ' 个墓碑' : '') + '。旧文件删除。被重写的数据就是写放大。', { hl: h2 });
  }

  function write(m, k, v, del, rec) {
    var e = { k: k, v: v, del: del };
    var at = -1;
    m.mem.forEach(function (x, i) { if (x.k === k) at = i; });
    if (at >= 0) m.mem[at] = e; else { m.mem.push(e); sortEntries(m.mem); }
    var h = {}; h['m' + k] = 'new';
    rec.step(1, (del ? '删除 ' + k + '：写一个墓碑' : '写入 ' + k + '=' + v) + '（先追加到 WAL，再放进内存表）' + (at >= 0 ? '。内存表中已经有 ' + k + '，直接覆盖。' : '。'), { hl: h });
    if (m.mem.length >= MEM_CAP) {
      var hm = {};
      m.mem.forEach(function (x) { hm['m' + x.k] = 'cur'; });
      rec.step(2, '内存表满了：按顺序整个写成一个 L0 的 SSTable，换一个新的 WAL。', { hl: hm });
      m.l0.push(m.mem);
      m.mem = [];
      var h2 = {}; h2['f0_' + (m.l0.length - 1)] = 'new';
      rec.step(3, '新的 L0 文件。它和别的 L0 文件的键范围可能重叠。', { hl: h2 });
      if (m.l0.length >= L0_TRIGGER) compact(m, rec);
    }
  }

  var BATCH = [['d', 1], ['a', 1], ['f', 1], ['b', 1], ['d', 2], ['c', 1]];

  DSViz.register('lsm-flow', {
    title: 'LSM 树：写入、落盘与合并',
    height: 250,
    width: 640,
    code: [
      '写 WAL，放进内存表',
      '内存表满了：落盘',
      '成为 L0 的一个文件',
      'L0 文件够多：和 L1 归并',
      '新文件替换旧文件'
    ],
    init: flowInit,
    view: flowView,
    start: function (m, rec) { BATCH.forEach(function (p) { write(m, p[0], p[1], false, rec); }); },
    actions: [
      { label: '写入', fields: [{ key: 'k', label: '键', value: 'e', size: 2 }, { key: 'v', label: '值', value: 3, size: 2 }],
        check: function (m, p) { if (!/^[a-z]$/.test(String(p.k))) return '键请输入一个小写字母'; },
        run: function (m, p, rec) { write(m, String(p.k), p.v, false, rec); } },
      { label: '删除', fields: [{ key: 'k', label: '键', value: 'a', size: 2 }],
        check: function (m, p) { if (!/^[a-z]$/.test(String(p.k))) return '键请输入一个小写字母'; },
        run: function (m, p, rec) { write(m, String(p.k), 0, true, rec); } },
      { label: '重置', run: function (m, p, rec) { var e = flowInit(); Object.keys(e).forEach(function (k) { m[k] = e[k]; }); rec.step(1, '重新开始：空的数据库。', {}); } }
    ]
  });

  /* ---------- 布隆过滤器 ---------- */

  var BITS = 16, K = 3;

  function hashes(word) {
    var h = 0;
    for (var i = 0; i < word.length; i++) h = (h * 31 + word.charCodeAt(i)) >>> 0;
    var d = ((h >>> 5) | 1) >>> 0, out = [];
    for (var j = 0; j < K; j++) out.push((h + j * d) % BITS);
    return out;
  }

  function bloomInit() { return { bits: [], words: [] }; }

  function bloomView(m, x) {
    var boxes = [], texts = [], hl = x.hl || {};
    texts.push(txt('t', 20, 20, '位数组（16 位）；每个键用 3 个哈希函数', 'muted'));
    for (var i = 0; i < BITS; i++) {
      boxes.push(box('b' + i, 20 + i * 37, 30, 33, 34, m.bits[i] ? '1' : '0', hl['b' + i] || (m.bits[i] ? 'cur' : 'dim')));
      texts.push(txt('i' + i, 36 + i * 37, 82, String(i), 'muted', 'middle'));
    }
    texts.push(txt('w', 20, 112, '已加入：' + (m.words.length ? m.words.join('、') : '（无）'), null));
    if (x.note) texts.push(txt('n', 20, 140, x.note, null));
    return { kind: 'free', boxes: boxes, texts: texts, vars: x.vars || {} };
  }

  function bloomAdd(m, w, rec) {
    var hs = hashes(w), h = {};
    hs.forEach(function (p) { m.bits[p] = 1; h['b' + p] = 'new'; });
    m.words.push(w);
    rec.step(1, '加入 ' + w + '：3 个哈希值是 ' + hs.join('、') + '，把这 3 位置为 1。', { hl: h, vars: { 键: w } });
  }

  function bloomQuery(m, w, rec) {
    var hs = hashes(w), h = {}, zero = -1;
    hs.forEach(function (p) { h['b' + p] = m.bits[p] ? 'ok' : 'warn'; if (!m.bits[p] && zero < 0) zero = p; });
    var added = m.words.indexOf(w) >= 0;
    if (zero >= 0) {
      rec.step(3, '查询 ' + w + '：位置 ' + hs.join('、') + '，其中第 ' + zero + ' 位是 0。一定没有加入过，不用读这个文件。', { hl: h, note: '结论：一定没有', vars: { 键: w } });
    } else {
      rec.step(4, '查询 ' + w + '：位置 ' + hs.join('、') + ' 都是 1。可能有，要去读数据块确认。' + (added ? '（它确实加入过。）' : '（其实它没有加入过：这些位是被别的键置成 1 的，这就是假阳性，白读一次。）'),
        { hl: h, note: added ? '结论：可能有（确实有）' : '结论：可能有（假阳性）', vars: { 键: w } });
    }
  }

  DSViz.register('lsm-bloom', {
    title: '布隆过滤器：一定没有，或者可能有',
    height: 160,
    width: 640,
    code: [
      '加入：算出 3 个位置，置为 1',
      '查询：算出同样的 3 个位置',
      '有一位是 0：一定没有',
      '全是 1：可能有（也可能是假阳性）'
    ],
    init: bloomInit,
    view: bloomView,
    start: function (m, rec) {
      ['apple', 'pear', 'fig', 'plum'].forEach(function (w) { bloomAdd(m, w, rec); });
      bloomQuery(m, 'pear', rec);
      bloomQuery(m, 'kiwi', rec);
      // 找一个没有加入、却全部命中的词，演示假阳性
      var cands = ['lime', 'date', 'grape', 'melon', 'peach', 'mango', 'cherry', 'lemon', 'guava', 'olive', 'berry', 'nut', 'yam', 'corn', 'bean'];
      for (var i = 0; i < cands.length; i++) {
        var hs = hashes(cands[i]);
        if (hs.every(function (p) { return m.bits[p]; })) { bloomQuery(m, cands[i], rec); break; }
      }
    },
    actions: [
      { label: '加入', fields: [{ key: 'w', label: '键', value: 'kiwi', size: 6 }],
        check: function (m, p) { if (!String(p.w).length) return '请输入一个键'; },
        run: function (m, p, rec) { bloomAdd(m, String(p.w), rec); } },
      { label: '查询', fields: [{ key: 'w', label: '键', value: 'lemon', size: 6 }],
        check: function (m, p) { if (!String(p.w).length) return '请输入一个键'; },
        run: function (m, p, rec) { bloomQuery(m, String(p.w), rec); } },
      { label: '清空', run: function (m, p, rec) { m.bits = []; m.words = []; rec.step(1, '清空。', {}); } }
    ]
  });
})();
