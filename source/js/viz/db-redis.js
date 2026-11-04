/* 《Redis》一章的逐步演示
 *
 * redis-loop      单线程事件循环：一条慢命令让后面的命令都在排队；换成 SCAN 以后
 * redis-listpack  listpack 中顺序查找 vs 哈希表中直接定位
 * redis-evict     近似 LRU：随机抽样 + 淘汰池
 * redis-cache     旁路缓存：读时未命中查数据库并写回，写时先更新数据库再删除缓存
 */
(function () {
  'use strict';

  function box(id, x, y, w, h, text, hl, sub) { return { id: id, x: x, y: y, w: w, h: h, text: text, hl: hl || null, sub: sub }; }
  function txt(id, x, y, text, cls, anchor) { return { id: id, x: x, y: y, text: text, cls: cls || null, anchor: anchor || 'start' }; }

  /* ---------- 单线程事件循环 ---------- */

  var CLIENTS = ['A', 'B', 'C'];

  function loopView(m, x) {
    var boxes = [], texts = [], arrows = [];
    texts.push(txt('cl', 20, 24, '客户端', 'muted'));
    CLIENTS.forEach(function (c, i) {
      var w = x.wait && x.wait[c] != null ? '等了 ' + x.wait[c] : null;
      boxes.push(box('c' + c, 20, 40 + i * 66, 70, 34, c, (x.chl || {})[c], w));
    });
    texts.push(txt('ql', 150, 24, '命令队列（按到达顺序）', 'muted'));
    (x.queue || []).forEach(function (q, i) {
      boxes.push(box('q' + q.id, 150 + i * 92, 40, 86, 36, q.c + ': ' + q.cmd, q.slow ? 'warn' : null));
    });
    texts.push(txt('el', 150, 132, '主线程（同一时刻只执行一条）', 'muted'));
    boxes.push(box('exec', 150, 148, 200, 40, x.running ? x.running.c + ': ' + x.running.cmd : '空闲', x.running ? (x.running.slow ? 'warn' : 'cur') : 'dim'));
    if (x.running) arrows.push({ from: 'c' + x.running.c, to: 'exec', hl: 'ok' });
    texts.push(txt('clock', 380, 172, '时间：' + x.clock, null));
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts, vars: x.vars || {} };
  }

  function loopRun(useScan, rec) {
    var seq = 0, clock = 0, queue = [], wait = {};
    function q(c, cmd, cost, slow) { return { id: seq++, c: c, cmd: cmd, cost: cost, slow: !!slow, arrive: clock }; }
    function fmt(us) { return us >= 1000 ? (us / 1000).toFixed(1) + ' 毫秒' : us.toFixed(1) + ' 微秒'; }
    function snap(line, msg, extra) {
      rec.step(line, msg, Object.assign({ queue: queue.slice(), clock: fmt(clock), wait: Object.assign({}, wait) }, extra || {}));
    }
    queue.push(q('A', 'GET x', 0.2));
    if (useScan) {
      queue.push(q('B', 'SCAN', 110));
    } else {
      queue.push(q('B', 'KEYS *', 109000, true));
    }
    queue.push(q('C', 'PING', 0.1));
    snap(2, 'epoll_wait 返回：三个客户端都发来了命令。' + (useScan ? 'B 不再用 KEYS，而是用 SCAN 分批遍历，每次只遍历一小部分。' : 'B 发的是 KEYS *，要遍历 100 万个 key。'), { chl: { A: 'cmp', B: 'cmp', C: 'cmp' } });
    var scans = 0;
    while (queue.length) {
      var cur = queue.shift();
      snap(6, '开始执行 ' + cur.c + ' 的 ' + cur.cmd + '。' + (cur.slow ? '这条命令要 109 毫秒，执行期间队列中的命令只能等。' : ''), { running: cur });
      clock += cur.cost;
      var hl = {}; hl[cur.c] = 'ok';
      if (cur.c === 'B' && useScan) {
        scans++;
        if (scans < 3) {
          // SCAN 返回游标，客户端拿到回复后再发下一批：排到队尾，C 的 PING 可以插进来
          snap(7, 'SCAN 只遍历了一小批（约 0.1 毫秒），带着游标返回。B 收到以后再发下一次 SCAN，排在队尾。', { chl: hl });
          queue.push(q('B', 'SCAN', 110));
          continue;
        }
      }
      wait[cur.c] = fmt(clock - cur.arrive);
      snap(7, cur.c + ' 的 ' + cur.cmd + ' 执行完，回复写进输出缓冲区。' + cur.c + ' 从发出命令到得到回复，在服务器上等了 ' + fmt(clock - cur.arrive) + '。', { chl: hl });
    }
    snap(9, useScan ? 'C 的 PING 只等了 0.1 毫秒左右：SCAN 把一次长时间的遍历拆成了很多小步，别人的命令可以插在中间。（本章实测：PING 最慢 1.1 毫秒）'
      : '最简单的 PING 也等了 109 毫秒：它排在 KEYS * 后面。单线程的服务器上，一条慢命令挡住所有人。（本章实测：PING 最慢 109.0 毫秒）');
  }

  DSViz.register('redis-loop', {
    title: '单线程事件循环：一条慢命令挡住所有人',
    height: 240,
    code: [
      'while (1) {',
      '    n = epoll_wait(...);              // 哪些连接上有命令',
      '    for (每个可读的连接)',
      '        读出命令，按顺序排队;',
      '    while (队列不空) {',
      '        执行一条命令;                  // 同一时刻只有这一条',
      '        回复放进这个客户端的输出缓冲区;',
      '    }',
      '    把回复写回 socket; 处理定时任务;',
      '}'
    ],
    init: function () { return {}; },
    view: loopView,
    start: function (m, rec) { loopRun(false, rec); },
    actions: [
      { label: 'B 执行 KEYS *', run: function (m, p, rec) { loopRun(false, rec); } },
      { label: 'B 改用 SCAN', run: function (m, p, rec) { loopRun(true, rec); } }
    ]
  });

  /* ---------- listpack 与哈希表 ---------- */

  var LP_FIELDS = ['name', 'age', 'city', 'email', 'phone', 'level'];
  var LP_VALUES = ['alice', '30', 'bj', 'a@x', '138', '7'];
  var NB = 8;
  function lpHash(s) { var h = 0; for (var i = 0; i < s.length; i++) h = (h * 31 + s.charCodeAt(i)) >>> 0; return h % NB; }

  function lpView(m, x) {
    var boxes = [], texts = [], hl = x.hl || {}, bh = x.bhl || {};
    texts.push(txt('t1', 20, 22, 'listpack：字段和值依次排在一块连续的内存中', 'muted'));
    boxes.push(box('lph', 20, 32, 44, 34, '头', 'dim'));
    LP_FIELDS.forEach(function (f, i) {
      boxes.push(box('lf' + i, 70 + i * 100, 32, 50, 34, f, hl['f' + i]));
      boxes.push(box('lv' + i, 120 + i * 100, 32, 46, 34, LP_VALUES[i], hl['v' + i]));
    });
    texts.push(txt('t2', 20, 112, '哈希表：先算哈希值，直接定位到桶，再比较这个桶中的几个字段', 'muted'));
    for (var b = 0; b < NB; b++) {
      boxes.push(box('b' + b, 20 + b * 74, 124, 66, 26, '[' + b + ']', bh[b]));
      var k = 0;
      LP_FIELDS.forEach(function (f, i) {
        if (lpHash(f) === b) {
          boxes.push(box('h' + i, 20 + b * 74, 158 + k * 34, 66, 28, f, hl['h' + i]));
          k++;
        }
      });
    }
    return { kind: 'free', boxes: boxes, texts: texts, vars: x.vars || {} };
  }

  function lpRun(m, p, rec) {
    var key = String(p.f), hl = {}, cmp = 0;
    rec.step(1, 'HGET user:1 ' + key + '：先在 listpack 中找。', { vars: { field: key } });
    var found = -1;
    for (var i = 0; i < LP_FIELDS.length; i++) {
      cmp++;
      hl['f' + i] = 'cmp';
      var eq = LP_FIELDS[i] === key;
      rec.step(3, '比较第 ' + (i + 1) + ' 个字段 ' + LP_FIELDS[i] + (eq ? '：相等。' : '：不是，跳过它和它的值，看下一个。'), { hl: Object.assign({}, hl), vars: { field: key, '比较次数': cmp } });
      if (eq) { found = i; hl['f' + i] = 'ok'; hl['v' + i] = 'ok'; break; }
      hl['f' + i] = 'dim'; hl['v' + i] = 'dim';
    }
    rec.step(found >= 0 ? 4 : 6, found >= 0 ? '找到了，值是 ' + LP_VALUES[found] + '。listpack 中比较了 ' + cmp + ' 次：字段越多，比较越多，O(n)。'
      : 'listpack 中比较了全部 ' + cmp + ' 个字段，没有找到。', { hl: Object.assign({}, hl), vars: { field: key, '比较次数': cmp } });
    var b = lpHash(key), bhl = {}; bhl[b] = 'cur';
    var hl2 = {}, cmp2 = 0;
    rec.step(9, '换成哈希表：hash("' + key + '") 落在第 ' + b + ' 个桶，直接跳过去，不看其他桶。', { bhl: bhl, vars: { field: key, '桶': b } });
    var hit = -1;
    LP_FIELDS.forEach(function (f, i) {
      if (lpHash(f) !== b || hit >= 0) return;
      cmp2++;
      hl2['h' + i] = f === key ? 'ok' : 'cmp';
      if (f === key) hit = i;
    });
    rec.step(10, hit >= 0 ? '桶中比较了 ' + cmp2 + ' 次就找到了 ' + key + '。哈希表的查找和字段总数无关：本章实测始终 0.04 微秒；listpack 在 512 个字段时 0.83 微秒，10000 个时 12.68 微秒。'
      : '这个桶中比较了 ' + cmp2 + ' 次，没有 ' + key + '。', { bhl: bhl, hl: hl2, vars: { field: key, '比较次数': cmp2 } });
  }

  DSViz.register('redis-listpack', {
    title: 'HGET：listpack 中顺序查找，哈希表中直接定位',
    height: 260,
    width: 720,
    code: [
      '/* listpack */',
      'for (p = 第一个元素; p != 末尾; p = 下一个值的后面) {',
      '    if (字段 p == field)',
      '        return p 后面的值;',
      '}',
      'return NULL;',
      '',
      '/* 哈希表 */',
      'b = hash(field) & (桶数 - 1);',
      'for (e = 桶[b]; e; e = e->next) if (e->field == field) return e->value;'
    ],
    init: function () { return {}; },
    view: lpView,
    start: function (m, rec) { lpRun(m, { f: 'phone' }, rec); },
    actions: [{ label: 'HGET', fields: [{ key: 'f', label: '字段', value: 'phone', size: 7 }],
      check: function (m, p) { if (!String(p.f).length || String(p.f).length > 8) return '请输入 1～8 个字符'; },
      run: lpRun }]
  });

  /* ---------- 近似 LRU ---------- */

  var EV_N = 20, POOL = 6;

  function evInit() {
    var idle = [12, 3, 45, 7, 30, 1, 60, 18, 5, 26, 9, 52, 2, 14, 38, 6, 21, 11, 4, 33];   // 每个 key 已经多少秒没被访问
    return { idle: idle.slice(), alive: idle.map(function () { return true; }), pool: [], seed: 7 };
  }
  function evRand(m) { m.seed = (m.seed * 1103515245 + 12345) & 0x7fffffff; return m.seed; }

  function evView(m, x) {
    var boxes = [], texts = [], hl = x.hl || {};
    texts.push(txt('t1', 20, 20, '所有的 key（数字是空闲时间：多少秒没被访问）', 'muted'));
    for (var i = 0; i < EV_N; i++) {
      var col = i % 10, row = Math.floor(i / 10);
      boxes.push(box('k' + i, 20 + col * 66, 30 + row * 56, 58, 30, 'k' + i, m.alive[i] ? hl[i] : 'del', m.alive[i] ? m.idle[i] + ' 秒' : '已淘汰'));
    }
    texts.push(txt('t2', 20, 160, '淘汰池（按空闲时间排序，最右边的先淘汰）', 'muted'));
    for (var j = 0; j < POOL; j++) {
      var e = m.pool[j];
      boxes.push(box('p' + j, 20 + j * 80, 170, 72, 30, e != null ? 'k' + e + ' · ' + m.idle[e] : '', e != null ? ((x.phl || {})[j] || null) : 'dim'));
    }
    return { kind: 'free', boxes: boxes, texts: texts, vars: x.vars || {} };
  }

  function evOnce(m, rec, samples) {
    var hl = {}, picked = [];
    while (picked.length < samples) {
      var k = evRand(m) % EV_N;
      if (m.alive[k] && picked.indexOf(k) < 0) picked.push(k);
    }
    picked.forEach(function (k) { hl[k] = 'cmp'; });
    var trueLRU = -1;
    for (var i = 0; i < EV_N; i++) if (m.alive[i] && (trueLRU < 0 || m.idle[i] > m.idle[trueLRU])) trueLRU = i;
    rec.step(2, '内存超过上限，要淘汰一个 key。随机抽取 ' + samples + ' 个：' + picked.map(function (k) { return 'k' + k + '（' + m.idle[k] + ' 秒）'; }).join('、') + '。', { hl: hl, vars: { samples: samples } });
    picked.forEach(function (k) { if (m.pool.indexOf(k) < 0) m.pool.push(k); });
    m.pool.sort(function (a, b) { return m.idle[a] - m.idle[b]; });
    while (m.pool.length > POOL) m.pool.shift();          // 池满了，空闲时间最短的被挤出去
    rec.step(3, '把它们放进淘汰池，按空闲时间排序；池满了，空闲时间短的被挤出去。池中还留着上几轮的候选。', { hl: hl, phl: (function () { var o = {}; o[m.pool.length - 1] = 'warn'; return o; })() });
    var victim = m.pool.pop();
    m.alive[victim] = false;
    var hl2 = {}; hl2[trueLRU] = trueLRU === victim ? null : 'warn';
    rec.step(4, '淘汰池中最右边的 k' + victim + '（' + m.idle[victim] + ' 秒）被淘汰。' + (victim === trueLRU ? '它正好就是真正最久没用的 key。'
      : '真正最久没用的是 k' + trueLRU + '（' + m.idle[trueLRU] + ' 秒，红框），这一次没抽到它，所以是「近似」的 LRU。'), { hl: hl2, vars: { samples: samples } });
  }

  DSViz.register('redis-evict', {
    title: '近似 LRU：随机抽样 + 淘汰池',
    height: 230,
    width: 700,
    code: [
      'while (used_memory > maxmemory) {',
      '    随机抽取 maxmemory-samples 个 key;',
      '    放进淘汰池（16 个位置，按空闲时间排序）;',
      '    淘汰池中空闲时间最长的 key 被删除;',
      '}'
    ],
    init: evInit,
    view: evView,
    start: function (m, rec) { evOnce(m, rec, 5); },
    actions: [
      { label: '再淘汰一个', fields: [{ key: 's', label: 'samples', value: 5, size: 3 }],
        check: function (m, p) {
          if (typeof p.s !== 'number' || p.s < 1 || p.s > 10) return '请输入 1～10';
          if (m.alive.filter(Boolean).length <= p.s) return 'key 不够了，先重置';
        },
        run: function (m, p, rec) { evOnce(m, rec, p.s); } },
      { label: '重置', run: function (m, p, rec) { var e = evInit(); m.idle = e.idle; m.alive = e.alive; m.pool = []; m.seed = 7; rec.step(1, '重置。', {}); } }
    ]
  });

  /* ---------- 旁路缓存 ---------- */

  function cacheView(m, x) {
    var boxes = [], arrows = [], texts = [], hl = x.hl || {};
    boxes.push(box('app', 20, 90, 90, 40, '应用', hl.app));
    boxes.push(box('redis', 280, 30, 110, 40, 'Redis', hl.redis));
    boxes.push(box('mysql', 280, 150, 110, 40, 'MySQL', hl.mysql));
    if (x.arrow) arrows.push({ from: 'app', to: x.arrow, hl: 'ok', label: x.label });
    texts.push(txt('rc', 410, 46, '缓存：' + (Object.keys(m.cache).length ? Object.keys(m.cache).map(function (k) { return 'product:' + k + ' = ' + m.cache[k]; }).join('，') : '（空）'), null));
    texts.push(txt('dc', 410, 166, '数据库：' + Object.keys(m.db).map(function (k) { return 'id ' + k + ' 价格 ' + m.db[k]; }).join('，'), null));
    texts.push(txt('st', 20, 222, '查数据库 ' + m.dbq + ' 次，缓存命中 ' + m.hits + ' 次', 'muted'));
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts, vars: x.vars || {} };
  }

  function cacheRead(m, id, rec) {
    var vars = { op: '读 ' + id };
    rec.step(2, '先查缓存：GET product:' + id + '。', { arrow: 'redis', label: 'GET', hl: { redis: 'cmp' }, vars: vars });
    if (m.cache[id] != null) {
      m.hits++;
      rec.step(3, '命中，直接返回 ' + m.cache[id] + '，不访问数据库。', { hl: { redis: 'ok', app: 'ok' }, vars: vars });
      return;
    }
    rec.step(4, '未命中，查数据库。', { arrow: 'mysql', label: 'SELECT', hl: { redis: 'warn', mysql: 'cmp' }, vars: vars });
    m.dbq++;
    if (m.db[id] == null) {
      rec.step(5, '数据库中也没有。如果不缓存一个空值，下次还会查数据库（缓存穿透）。', { hl: { mysql: 'warn' }, vars: vars });
      return;
    }
    var v = m.db[id];
    m.cache[id] = v;
    rec.step(6, '读到 ' + v + '，写回缓存并设置过期时间：SET product:' + id + ' ' + v + ' EX 3600。下次读就会命中。', { arrow: 'redis', label: 'SET … EX', hl: { redis: 'new' }, vars: vars });
  }

  function cacheWrite(m, id, price, rec) {
    var vars = { op: '改 ' + id + ' → ' + price };
    m.db[id] = price;
    rec.step(10, '先更新数据库：UPDATE product SET price = ' + price + ' WHERE id = ' + id + '。', { arrow: 'mysql', label: 'UPDATE', hl: { mysql: 'new' }, vars: vars });
    delete m.cache[id];
    rec.step(11, '再删除缓存：DEL product:' + id + '。不是更新缓存：下一次读会从数据库加载新值。', { arrow: 'redis', label: 'DEL', hl: { redis: 'warn' }, vars: vars });
  }

  function cacheInit() { return { db: { 1: 100, 2: 300 }, cache: {}, dbq: 0, hits: 0 }; }

  DSViz.register('redis-cache', {
    title: '旁路缓存：读时先查缓存，写时先改数据库再删缓存',
    height: 240,
    width: 720,
    code: [
      'getProduct(id):',
      '    v = GET product:id',
      '    if (v) return v;                       // 命中',
      '    v = SELECT … FROM product WHERE id = ?;',
      '    if (!v) return NULL;',
      '    SET product:id v EX ttl;              // 写回缓存，带过期时间',
      '    return v;',
      '',
      'updatePrice(id, price):',
      '    UPDATE product SET price = ? WHERE id = ?;',
      '    DEL product:id;'
    ],
    init: cacheInit,
    view: cacheView,
    start: function (m, rec) {
      rec.step(1, '数据库中有两个商品，缓存是空的。', { vars: {} });
      cacheRead(m, 1, rec);
      cacheRead(m, 1, rec);
      cacheWrite(m, 1, 120, rec);
      cacheRead(m, 1, rec);
    },
    actions: [
      { label: '读', fields: [{ key: 'id', label: 'id', value: 2, size: 3 }],
        check: function (m, p) { if (typeof p.id !== 'number' || p.id < 1 || p.id > 9) return '请输入 1～9'; },
        run: function (m, p, rec) { cacheRead(m, p.id, rec); } },
      { label: '改价格', fields: [{ key: 'id', label: 'id', value: 2, size: 3 }, { key: 'price', label: '价格', value: 280, size: 5 }],
        check: function (m, p) {
          if (typeof p.id !== 'number' || m.db[p.id] == null) return '只能改 id 1 或 2';
          if (typeof p.price !== 'number') return '价格要是数字';
        },
        run: function (m, p, rec) { cacheWrite(m, p.id, p.price, rec); } },
      { label: '重置', run: function (m, p, rec) { var e = cacheInit(); m.db = e.db; m.cache = e.cache; m.dbq = 0; m.hits = 0; rec.step(1, '重置。', {}); } }
    ]
  });
})();
