/* 实战项目《地铁换乘查询（一）》：逐行解析数据文件；station 命令依次经过哪些结构 */
(function () {
  'use strict';

  var GOOD = ['LINE A线', '甲', '乙 2', '丙 3', 'END', 'LINE B线', '丁', '乙 4', 'END'];
  var BAD = ['LINE A线', '甲', '乙 2', '甲 3', 'END'];
  var NBUCKETS = 8;

  function fnv(s) {                                 // FNV-1a，按 UTF-8 字节计算，和 C 程序的结果相同
    var bytes = new TextEncoder().encode(s), h = 2166136261;
    for (var i = 0; i < bytes.length; i++) { h ^= bytes[i]; h = Math.imul(h, 16777619) >>> 0; }
    return h >>> 0;
  }
  function hex(h) { return '0x' + ('00000000' + h.toString(16)).slice(-8); }

  // 和 metro.c 相同的建图过程，每一步通过 rec.step 记录下来
  function newMetro() { return { stations: [], lines: [], index: {}, lineIndex: {}, nseg: 0 }; }
  function getOrAdd(m, name) {
    if (name in m.index) return { id: m.index[name], fresh: false };
    var id = m.stations.length;
    m.stations.push({ name: name, edges: [] });
    m.index[name] = id;
    return { id: id, fresh: true };
  }

  /* ---------- 演示一：逐行解析 ---------- */

  function parseView(m, x) {
    var boxes = [], texts = [], file = x.file || GOOD;
    texts.push({ id: 'f', x: 20, y: 28, text: '数据文件', cls: 'muted' });
    file.forEach(function (ln, i) {
      texts.push({ id: 'n' + i, x: 36, y: 44 + i * 30 + 18, text: String(i + 1), cls: 'muted', anchor: 'end' });
      boxes.push({ id: 'l' + i, x: 44, y: 44 + i * 30, w: 130, h: 26, text: ln,
        hl: i === x.at ? (x.err ? 'warn' : 'cur') : (x.at != null && i > x.at ? 'dim' : null) });
    });
    texts.push({ id: 's', x: 210, y: 28, text: 'stations[]：每个车站和它的邻接表 edges', cls: 'muted' });
    m.stations.forEach(function (s, k) {
      var y = 44 + k * 34;
      boxes.push({ id: 's' + k, x: 210, y: y, w: 74, h: 26, text: '[' + k + '] ' + s.name, hl: (x.st || {})[k] });
      s.edges.forEach(function (e, j) {
        boxes.push({ id: 'e' + k + '-' + j, x: 294 + j * 88, y: y, w: 84, h: 26,
          text: '→' + m.stations[e.to].name + ' ' + m.lines[e.line].name + ' ' + e.minutes,
          hl: (x.ed || {})[k + '-' + j] });
      });
    });
    var y0 = 44 + Math.max(m.stations.length, 1) * 34 + 22;
    texts.push({ id: 'hi', x: 210, y: y0, text: 'stationIndex：' + (Object.keys(m.index).map(function (k) { return k + '→' + m.index[k]; }).join('　') || '（空）') });
    texts.push({ id: 'li', x: 210, y: y0 + 24, text: 'lineIndex：' + (Object.keys(m.lineIndex).map(function (k) { return k + '→' + m.lineIndex[k]; }).join('　') || '（空）') });
    m.lines.forEach(function (L, i) {
      texts.push({ id: 'L' + i, x: 210, y: y0 + 54 + i * 24,
        text: 'lines[' + i + '] ' + L.name + '：stations {' + L.stations.join(', ') + '}　minutes {' + L.minutes.join(', ') + '}' });
    });
    return {
      kind: 'free', boxes: boxes, texts: texts,
      vars: { lineno: x.at == null ? 0 : x.at + 1, cur: m.cur, nsegments: m.nseg }
    };
  }

  function parseRun(m, file, rec) {
    Object.assign(m, newMetro());
    m.cur = -1;
    rec.step(1, '开始读取数据文件。cur = -1 表示现在不在任何线路中。', { file: file });
    for (var i = 0; i < file.length; i++) {
      var parts = file[i].split(' '), tok = parts[0], arg = parts[1];
      var base = { file: file, at: i };
      rec.step(3, '读入第 ' + (i + 1) + ' 行，拆出 tok = "' + tok + '"' + (arg ? '，arg = "' + arg + '"' : '，没有 arg') + '。', base);
      if (tok === 'LINE') {
        var li = m.lines.length;
        m.lines.push({ name: arg, stations: [], minutes: [] });
        m.lineIndex[arg] = li;
        m.cur = li;
        rec.step(5, 'addLine：新建线路 ' + arg + '，编号是 ' + li + '，同时放进 lineIndex。cur = ' + li + '。', base);
      } else if (tok === 'END') {
        var ended = m.lines[m.cur];
        m.cur = -1;
        rec.step(7, '线路 ' + ended.name + ' 结束（它有 ' + ended.stations.length + ' 个车站，不少于 2 个）。cur = -1。', base);
      } else {
        var minutes = arg ? +arg : 0, L = m.lines[m.cur];
        var r = getOrAdd(m, tok), st = {};
        st[r.id] = r.fresh ? 'new' : 'cmp';
        rec.step(9, r.fresh
          ? tok + ' 不在 stationIndex 中：新建车站，编号是 ' + r.id + '，同时放进 stationIndex。'
          : tok + ' 已经在 stationIndex 中，编号是 ' + r.id + '：直接用这个编号，不再新建。', Object.assign({ st: st }, base));
        if (L.stations.indexOf(r.id) >= 0) {
          rec.step(11, '车站 ' + r.id + ' 已经在 ' + L.name + ' 的 stations 中了 → 报错「数据错误（第 ' + (i + 1) + ' 行）：车站 ' + tok + ' 在线路 ' + L.name + ' 中重复」，释放已经建好的部分，返回 -1。',
            Object.assign({ st: st, err: true }, base));
          return;
        }
        L.stations.push(r.id);
        L.minutes.push(L.stations.length === 1 ? 0 : minutes);
        if (L.stations.length === 1) {
          rec.step(12, tok + ' 是 ' + L.name + ' 的首站：加到线路的末尾，前面没有车站，不连边。', Object.assign({ st: st }, base));
        } else {
          var prev = L.stations[L.stations.length - 2], pe = m.stations[prev].edges, se = m.stations[r.id].edges, ed = {};
          pe.push({ to: r.id, line: m.cur, minutes: minutes });
          se.push({ to: prev, line: m.cur, minutes: minutes });
          ed[prev + '-' + (pe.length - 1)] = 'new';
          ed[r.id + '-' + (se.length - 1)] = 'new';
          m.nseg++;
          st[prev] = 'cmp';
          rec.step(12, tok + ' 加到 ' + L.name + ' 的末尾。它和上一站 ' + m.stations[prev].name + ' 之间是一个区间（' + minutes + ' 分钟）：在两个车站的邻接表末尾各追加一条边，nsegments = ' + m.nseg + '。',
            Object.assign({ st: st, ed: ed }, base));
        }
      }
    }
    rec.step(1, '文件读完了，cur 是 -1，没有缺少 END 的线路。解析成功：' + m.stations.length + ' 个车站、' + m.lines.length + ' 条线路、' + m.nseg + ' 个区间。', { file: file });
  }

  DSViz.register('metro-parse', {
    title: '逐行解析数据文件，建立邻接表',
    height: 330,
    code: [
      'while (fgets(buf, sizeof buf, fp)) {',
      '    ++lineno;',
      '    tok = 第一部分;  arg = 第二部分;',
      '    if (strcmp(tok, "LINE") == 0) {',
      '        cur = addLine(m, arg);',
      '    } else if (strcmp(tok, "END") == 0) {',
      '        cur = -1;',
      '    } else {',
      '        int s = getOrAddStation(m, tok);',
      '        if (lineHasStation(&m->lines[cur], s))',
      '            return loadError(..., "车站 %s 在线路 %s 中重复", ...);',
      '        appendStation(m, cur, s, minutes);     // 不是首站时连一条边',
      '    }',
      '}'
    ],
    init: function () { var m = newMetro(); m.cur = -1; return m; },
    view: parseView,
    start: function (m, rec) { parseRun(m, GOOD, rec); },
    actions: [
      { label: '解析正确的文件', run: function (m, p, rec) { parseRun(m, GOOD, rec); } },
      { label: '解析有错误的文件', run: function (m, p, rec) { parseRun(m, BAD, rec); } }
    ]
  });

  /* ---------- 演示二：station 命令依次经过哪些结构 ---------- */

  function built() {                                // 解析正确的文件后得到的结构
    var m = newMetro(), rec = { step: function () {} };
    parseRun(m, GOOD, rec);
    var order = [];                                 // 哈希表：按插入的顺序，每次插到桶的链表头部
    Object.keys(m.index).forEach(function (k) { order.push(k); });
    m.buckets = [];
    for (var b = 0; b < NBUCKETS; b++) m.buckets.push([]);
    order.forEach(function (k) { m.buckets[fnv(k) % NBUCKETS].unshift(k); });
    return m;
  }

  function lookupView(m, x) {
    var boxes = [], texts = [], arrows = [];
    texts.push({ id: 'h', x: 20, y: 28, text: 'stationIndex：8 个桶', cls: 'muted' });
    m.buckets.forEach(function (chain, b) {
      var y = 40 + b * 32;
      texts.push({ id: 'bi' + b, x: 52, y: y + 18, text: '[' + b + ']', cls: 'muted', anchor: 'end' });
      boxes.push({ id: 'b' + b, x: 58, y: y, w: 34, h: 26, text: chain.length ? '•' : '', hl: x.bucket === b ? 'cur' : (chain.length ? null : 'empty') });
      chain.forEach(function (k, j) {
        boxes.push({ id: 'c' + b + '-' + j, x: 112 + j * 104, y: y, w: 88, h: 26, text: k + ' → ' + m.index[k], hl: (x.chain || {})[b + '-' + j] });
        arrows.push({ from: j ? 'c' + b + '-' + (j - 1) : 'b' + b, to: 'c' + b + '-' + j, hl: x.bucket === b ? 'cur' : null });
      });
    });
    texts.push({ id: 's', x: 360, y: 28, text: 'stations[]：车站和邻接表 edges', cls: 'muted' });
    m.stations.forEach(function (s, k) {
      var y = 40 + k * 32;
      boxes.push({ id: 's' + k, x: 360, y: y, w: 70, h: 26, text: '[' + k + '] ' + s.name, hl: x.station === k ? 'cur' : null });
      s.edges.forEach(function (e, j) {
        boxes.push({ id: 'e' + k + '-' + j, x: 440 + j * 92, y: y, w: 86, h: 26,
          text: '→' + m.stations[e.to].name + ' ' + m.lines[e.line].name + ' ' + e.minutes,
          hl: x.station === k ? (x.edgeHl || 'cmp') : null });
      });
    });
    (x.out || []).forEach(function (line, i) {
      var shown = line.replace(/^ +/, function (sp) { return new Array(sp.length + 1).join('\u00a0'); });   // SVG 会吞掉开头的空格
      texts.push({ id: 'o' + i, x: 360, y: 200 + i * 22, text: shown, cls: i === 0 ? 'muted' : null });
    });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts, vars: x.vars || {} };
  }

  function lookupRun(m, p, rec) {
    var name = String(p.name == null ? '' : p.name).trim(), h = fnv(name), b = h % NBUCKETS;
    var vars = { key: '"' + name + '"' };
    rec.step(1, 'station ' + name + '：先用 metroFindStation 把站名转换成编号，它调用的是 hmGet(&m->stationIndex, "' + name + '")。', { vars: vars });
    vars = Object.assign({}, vars, { 'hashFNV(key)': hex(h), b: h + ' % 8 = ' + b });
    rec.step(3, '计算哈希值 ' + hex(h) + '，除以桶的个数 8 取余数，得到 ' + b + '：只需要在第 ' + b + ' 号桶的链表中找。',
      { bucket: b, vars: vars });
    var chain = m.buckets[b], found = -1, hl = {};
    for (var j = 0; j < chain.length; j++) {
      hl = {}; hl[b + '-' + j] = chain[j] === name ? 'ok' : 'cmp';
      if (chain[j] === name) {
        found = m.index[name];
        rec.step(5, 'strcmp("' + chain[j] + '", "' + name + '") == 0，找到了，返回编号 ' + found + '。', { bucket: b, chain: hl, vars: vars });
        break;
      }
      rec.step(5, 'strcmp("' + chain[j] + '", "' + name + '") ≠ 0，' + (j + 1 < chain.length ? '沿着 next 看下一个。' : '这是链表的最后一个元素。'),
        { bucket: b, chain: hl, vars: vars });
    }
    if (found < 0) {
      rec.step(7, (chain.length ? '链表走完了也没有找到' : '第 ' + b + ' 号桶是空的') + '，返回 -1。程序输出「找不到车站：' + name + '」。',
        { bucket: b, vars: vars, out: ['metro> station ' + name, '找不到车站：' + name] });
      return;
    }
    var s = m.stations[found];
    vars = Object.assign({}, vars, { s: found });
    rec.step(0, '有了编号，直接用下标取出 stations[' + found + ']，它的邻接表有 ' + s.edges.length + ' 条边。不需要再比较任何字符串。',
      { bucket: b, chain: hl, station: found, vars: vars });
    var groups = [], out = ['metro> station ' + name];
    s.edges.forEach(function (e) { if (groups[groups.length - 1] !== e.line) groups.push(e.line); });
    out.push(name + '：' + groups.map(function (l) { return m.lines[l].name; }).join('、') + (groups.length > 1 ? '（换乘站）' : ''));
    groups.forEach(function (l) {
      out.push('  ' + m.lines[l].name + '：' + s.edges.filter(function (e) { return e.line === l; })
        .map(function (e) { return m.stations[e.to].name + ' ' + e.minutes + ' 分钟'; }).join('、'));
    });
    rec.step(0, 'main.c 按邻接表的顺序遍历这些边，同一条线路的边挨在一起，所以可以按线路分组输出。',
      { bucket: b, chain: hl, station: found, edgeHl: 'ok', vars: vars, out: out });
  }

  DSViz.register('metro-lookup', {
    title: 'station 命令：从站名到相邻车站',
    width: 740,
    height: 300,
    code: [
      'int hmGet(const HashMap* m, const char* key)',
      '{',
      '    size_t b = hashFNV(key) % m->nbuckets;',
      '    for (const HmEntry* e = m->buckets[b]; e; e = e->next) {',
      '        if (strcmp(e->key, key) == 0) return e->value;',
      '    }',
      '    return -1;',
      '}'
    ],
    init: built,
    view: lookupView,
    start: function (m, rec) { lookupRun(m, { name: '乙' }, rec); },
    actions: [
      { label: '查询', fields: [{ key: 'name', label: '站名', value: '乙', size: 8 }],
        check: function (m, p) { return String(p.name).trim() ? null : '请输入站名'; },
        run: lookupRun }
    ]
  });
})();
