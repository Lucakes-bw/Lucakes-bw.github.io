/* 《数据库设计》一章后两篇的逐步演示
 *
 * ddl-algo     ALTER TABLE 的三种算法：改表期间到来的 UPDATE 要不要等
 * ddl-mdl      元数据锁：长事务 → ALTER 等待 → 后面所有的请求排队
 * part-prune   分区裁剪：哪些条件能裁剪、哪些不能
 * shard-query  分片查询：带分片键只访问一个分片；深分页要每个分片返回 10N 行
 */
(function () {
  'use strict';

  function box(id, x, y, w, h, text, hl, sub) { return { id: id, x: x, y: y, w: w, h: h, text: text, hl: hl || null, sub: sub }; }
  function txt(id, x, y, text, cls, anchor) { return { id: id, x: x, y: y, text: text, cls: cls || null, anchor: anchor || 'start' }; }

  /* ---------- ALTER TABLE 的三种算法 ---------- */

  var PHASES = {
    INSTANT: [['拿排他锁', 'warn'], ['改数据字典', 'cur'], ['提交', 'ok']],
    INPLACE: [['拿排他锁', 'warn'], ['降级：可读写', 'ok'], ['扫描、建索引', 'cur'], ['应用在线日志', 'cur'], ['排他锁，提交', 'warn']],
    COPY: [['拿排他锁', 'warn'], ['降级：只读', 'warn'], ['逐行复制', 'cur'], ['换名', 'cur'], ['提交', 'ok']]
  };

  function algoView(m, x) {
    var boxes = [], texts = [], ph = PHASES[x.algo || 'INPLACE'];
    texts.push(txt('t1', 20, 24, 'ALTER TABLE（' + (x.algo || 'INPLACE') + '）的各个阶段', 'muted'));
    ph.forEach(function (p, i) {
      boxes.push(box('p' + i, 20 + i * 132, 36, 124, 38, p[0], i === x.phase ? p[1] : (i < x.phase ? 'dim' : null)));
    });
    texts.push(txt('t2', 20, 112, '另一个会话：UPDATE orders SET amount = amount + 1 WHERE id = 12345', 'muted'));
    boxes.push(box('u', 20, 124, 260, 38, x.upd || '还没开始', x.uhl));
    if (x.log) boxes.push(box('log', 320, 124, 200, 38, '在线日志：' + x.log, 'new'));
    return { kind: 'free', boxes: boxes, texts: texts, vars: x.vars || {} };
  }

  function algoRun(algo, rec) {
    var ph = PHASES[algo], vars = { algo: algo };
    function s(line, msg, extra) { rec.step(line, msg, Object.assign({ algo: algo, vars: vars }, extra)); }
    if (algo === 'INSTANT') {
      s(1, 'INSTANT：只改表的元数据（数据字典），一行数据都不碰。', { phase: 0 });
      s(2, '改数据字典：几毫秒，和表有多大无关。', { phase: 1 });
      s(3, '完成。实测 400 万行加一列 0.00 秒；UPDATE 到来时改表早就结束了，不用等。', { phase: 2, upd: 'UPDATE 立即完成（0.01 秒）', uhl: 'ok' });
      return;
    }
    s(1, algo + '：开始时短暂地拿排他元数据锁，准备新的结构。', { phase: 0 });
    if (algo === 'INPLACE') {
      s(2, '锁降级为共享锁：别的会话可以照常读写。', { phase: 1 });
      s(3, '扫描全部数据，构建新的 B+ 树（或重建表）。这时 UPDATE 来了……', { phase: 2, upd: 'UPDATE 到达', uhl: 'cmp' });
      s(3, 'UPDATE 直接修改原表并立即返回；这次修改同时记进在线日志，以后补到新结构上。', { phase: 2, upd: 'UPDATE 立即完成（0.00 秒）', uhl: 'ok', log: '1 条修改' });
      s(4, '扫描完成，把在线日志中的修改应用到新结构上。', { phase: 3, upd: 'UPDATE 已完成', uhl: 'ok', log: '应用中' });
      s(5, '最后短暂地拿排他锁，切换到新结构。实测 400 万行加索引 1.55 秒，期间的 UPDATE 不用等。', { phase: 4, upd: 'UPDATE 已完成', uhl: 'ok' });
    } else {
      s(2, '锁降级，但只允许读：写入会破坏正在复制的数据。', { phase: 1 });
      s(3, '一行一行地复制到新结构的临时表。这时 UPDATE 来了……', { phase: 2, upd: 'UPDATE 到达', uhl: 'cmp' });
      s(3, 'UPDATE 要等：COPY 期间不允许写入。', { phase: 2, upd: 'UPDATE 等待中……', uhl: 'warn' });
      s(4, '复制完成，新表换名为 orders。', { phase: 3, upd: 'UPDATE 等待中……', uhl: 'warn' });
      s(5, '提交，释放锁，UPDATE 才得以执行。实测 400 万行改列类型 8.14 秒，UPDATE 等了 7.14 秒。', { phase: 4, upd: 'UPDATE 完成（等了 7.14 秒）', uhl: 'del' });
    }
  }

  DSViz.register('ddl-algo', {
    title: 'ALTER TABLE 的三种算法：改表期间的写入要不要等',
    height: 180,
    width: 700,
    code: [
      '拿排他元数据锁',
      '降级（INPLACE 允许读写；COPY 只允许读）',
      '扫描或复制数据（INSTANT 没有这一步）',
      '应用在线日志 / 换名',
      '拿排他锁，提交'
    ],
    init: function () { return {}; },
    view: algoView,
    start: function (m, rec) { algoRun('INPLACE', rec); },
    actions: [
      { label: 'INSTANT（加列）', run: function (m, p, rec) { algoRun('INSTANT', rec); } },
      { label: 'INPLACE（加索引）', run: function (m, p, rec) { algoRun('INPLACE', rec); } },
      { label: 'COPY（改类型）', run: function (m, p, rec) { algoRun('COPY', rec); } }
    ]
  });

  /* ---------- 元数据锁排队 ---------- */

  var SESS = [
    { id: 1, text: '会话 1：BEGIN; SELECT …（15 秒不提交）', lock: '共享' },
    { id: 2, text: '会话 2：ALTER TABLE … ADD COLUMN', lock: '排他' },
    { id: 3, text: '会话 3：SELECT … WHERE id = 2', lock: '共享' },
    { id: 4, text: '会话 4：UPDATE … WHERE id = 3', lock: '共享' }
  ];

  function mdlView(m, x) {
    var boxes = [], texts = [];
    texts.push(txt('h', 20, 22, 'orders 表的元数据锁', 'muted'));
    texts.push(txt('hh', 20, 46, '持有：', null));
    (x.hold || []).forEach(function (id, i) {
      var s = SESS[id - 1];
      boxes.push(box('s' + id, 70 + i * 300, 30, 290, 32, s.text, (x.hl || {})[id] || 'ok', s.lock + '锁'));
    });
    texts.push(txt('wh', 20, 116, '等待：', null));
    (x.wait || []).forEach(function (id, i) {
      var s = SESS[id - 1];
      boxes.push(box('s' + id, 70 + i * 205, 100, 198, 32, s.text.replace(/（.*）/, ''), (x.hl || {})[id] || 'warn', s.lock + '锁'));
    });
    texts.push(txt('clk', 20, 176, '时间：' + x.t + ' 秒' + (x.note ? '　' + x.note : ''), 'muted'));
    return { kind: 'free', boxes: boxes, texts: texts, vars: x.vars || {} };
  }

  function mdlRun(timeout, rec) {
    var vars = { lock_wait_timeout: timeout ? timeout + ' 秒' : '31536000（一年）' };
    function s(line, msg, extra) { rec.step(line, msg, Object.assign({ vars: vars }, extra)); }
    s(1, '会话 1 开了一个事务，读了 orders 的一行。共享元数据锁一直持有到事务结束。', { t: 0, hold: [1], wait: [] });
    s(2, '1 秒后，会话 2 执行 ALTER（INSTANT 加列，本身只要几毫秒），但它要排他锁，和会话 1 的共享锁冲突，开始等待。', { t: 1, hold: [1], wait: [2] });
    s(3, '2 秒后，会话 3 只是读一行。共享锁和会话 1 的共享锁不冲突，但等待队列中已经有一个排他锁的请求，它必须排在后面（防止 ALTER 饿死）。', { t: 2, hold: [1], wait: [2, 3, 4] });
    if (timeout) {
      s(4, 'ALTER 等了 ' + timeout + ' 秒，超过 lock_wait_timeout，报错放弃：ERROR 1205 Lock wait timeout exceeded。', { t: 1 + timeout, hold: [1], wait: [2, 3, 4], hl: { 2: 'del' } });
      s(5, '排他锁请求撤走了，会话 3、4 的共享锁和会话 1 兼容，立即执行。实测它们只多等了 2 秒左右。', { t: 1 + timeout, hold: [1, 3], wait: [], note: '会话 4 也已完成' });
      return;
    }
    s(4, '默认的 lock_wait_timeout 是一年：ALTER 一直等，后面所有访问 orders 的请求都在它后面排队。连接池会被逐渐占满。', { t: 10, hold: [1], wait: [2, 3, 4] });
    s(5, '15 秒时会话 1 提交，ALTER 拿到排他锁，瞬间完成。', { t: 15, hold: [2], wait: [3, 4], hl: { 2: 'cur' } });
    s(6, 'ALTER 释放锁，会话 3、4 终于执行。实测：ALTER 等了 13.96 秒，SELECT 12.95 秒，UPDATE 12.73 秒。', { t: 15, hold: [3], wait: [], note: '会话 4 也已完成' });
  }

  DSViz.register('ddl-mdl', {
    title: '元数据锁：一个长事务，加上一条 ALTER，挡住所有人',
    height: 190,
    width: 700,
    code: [
      'BEGIN; SELECT … FROM orders …;      -- 会话 1：共享锁，持有到 COMMIT',
      'ALTER TABLE orders ADD COLUMN c9 INT; -- 会话 2：要排他锁',
      'SELECT … FROM orders WHERE id = 2;   -- 会话 3：排在 ALTER 后面',
      '-- 等待……',
      'COMMIT;                              -- 会话 1 结束',
      '-- 排队的请求依次执行'
    ],
    init: function () { return {}; },
    view: mdlView,
    start: function (m, rec) { mdlRun(0, rec); },
    actions: [
      { label: '默认 lock_wait_timeout', run: function (m, p, rec) { mdlRun(0, rec); } },
      { label: 'SET lock_wait_timeout = 3', run: function (m, p, rec) { mdlRun(3, rec); } }
    ]
  });

  /* ---------- 分区裁剪 ---------- */

  var MONTHS = ['01', '02', '03', '04', '05', '06', '07', '08', '09', '10', '11', '12'];

  function pruneView(m, x) {
    var boxes = [], texts = [], parts = x.parts, hit = x.hit || {};
    texts.push(txt('q', 20, 22, x.sql, null));
    parts.forEach(function (p, i) {
      var col = i % 7, row = Math.floor(i / 7);
      boxes.push(box(p, 20 + col * 96, 40 + row * 58, 88, 34, p, hit[p] ? (hit[p] === 'empty' ? 'dim' : 'cmp') : null, hit[p] === 'empty' ? '空分区' : null));
    });
    var n = Object.keys(hit).length;
    texts.push(txt('n', 20, 172, '访问 ' + n + ' 个分区（共 ' + parts.length + ' 个）', n <= 2 ? null : 'muted'));
    return { kind: 'free', boxes: boxes, texts: texts, vars: x.vars || {} };
  }

  function partsList(withP0) {
    var a = withP0 ? ['p0'] : [];
    MONTHS.forEach(function (mm) { a.push('p2025' + mm); });
    a.push('pmax');
    return a;
  }

  function pruneRun(kind, rec) {
    var parts = partsList(kind === 'p0'), hit = {};
    if (kind === 'range' || kind === 'p0') {
      var sql = "WHERE created_at >= '2025-03-01' AND created_at < '2025-04-01'";
      rec.step(1, '条件是对分区键的范围比较，MySQL 算出它只落在 3 月的分区中。', { parts: parts, sql: sql, hit: { p202503: 1 } });
      if (kind === 'p0') {
        hit = { p0: 'empty', p202503: 1 };
        rec.step(2, 'NULL 会进第一个分区，所以第一个分区总会被带上。现在第一个分区是空的 p0（VALUES LESS THAN (0)），带上它几乎没有代价。', { parts: parts, sql: sql, hit: hit });
      } else {
        hit = { p202501: 1, p202503: 1 };
        rec.step(2, '但 EXPLAIN 显示 p202501,p202503：TO_DAYS(NULL) 是 NULL，NULL 放在第一个分区，MySQL 不能确定 1 月分区中没有 NULL，只好也扫描它（33 万行）。解决：第一个分区放一个空的 p0。', { parts: parts, sql: sql, hit: hit });
      }
    } else if (kind === 'func') {
      parts.forEach(function (p) { hit[p] = 1; });
      rec.step(3, '对分区键套了函数：YEAR(created_at)、MONTH(created_at) 的值无法反推出属于哪个分区，裁剪失效，13 个分区全部扫描。', { parts: parts, sql: 'WHERE YEAR(created_at) = 2025 AND MONTH(created_at) = 3', hit: hit });
    } else {
      parts.forEach(function (p) { hit[p] = 1; });
      rec.step(4, '条件中没有分区键：id = 12345 可能在任何一个月，每个分区的主键索引都要查一次。', { parts: parts, sql: 'WHERE id = 12345', hit: hit });
      rec.step(4, '加上分区键才能裁剪到一个分区（实测：p202509，type = const）。', { parts: parts, sql: "WHERE id = 12345 AND created_at = '2025-09-…'", hit: { p202509: 1 } });
    }
  }

  DSViz.register('part-prune', {
    title: '分区裁剪：哪些查询只访问一部分分区',
    height: 185,
    width: 700,
    code: [
      '对分区键的范围比较 → 只访问对应的分区',
      '第一个分区（可能有 NULL）总会被带上',
      '对分区键套了函数 → 不能裁剪',
      '条件中没有分区键 → 访问所有分区'
    ],
    init: function () { return {}; },
    view: pruneView,
    start: function (m, rec) { pruneRun('range', rec); },
    actions: [
      { label: '查 3 月', run: function (m, p, rec) { pruneRun('range', rec); } },
      { label: '加空分区 p0', run: function (m, p, rec) { pruneRun('p0', rec); } },
      { label: 'YEAR() / MONTH()', run: function (m, p, rec) { pruneRun('func', rec); } },
      { label: '按 id 查', run: function (m, p, rec) { pruneRun('id', rec); } }
    ]
  });

  /* ---------- 分片查询 ---------- */

  var SHARD_ROWS = [914091, 915041, 916954, 914324];

  function shardView(m, x) {
    var boxes = [], texts = [], arrows = [];
    boxes.push(box('app', 20, 90, 110, 40, '应用 / 中间件', 'plain'));
    SHARD_ROWS.forEach(function (n, i) {
      var r = (x.ret || [])[i];
      boxes.push(box('s' + i, 300, 20 + i * 48, 140, 36, '分片 ' + i, (x.hl || {})[i], null));
      texts.push(txt('n' + i, 450, 43 + i * 48, (n / 10000).toFixed(1) + ' 万行' + (r != null ? '　返回 ' + r.toLocaleString() + ' 行' : ''), r ? null : 'muted'));
      if ((x.hl || {})[i]) arrows.push({ from: 'app', to: 's' + i, hl: 'ok' });
    });
    if (x.total != null) texts.push(txt('tot', 20, 220, '合并 ' + x.total.toLocaleString() + ' 行，排序后取 10 行', x.total > 1000 ? null : 'muted'));
    return { kind: 'free', boxes: boxes, texts: texts, arrows: arrows, vars: x.vars || {} };
  }

  function shardRoute(uid, rec) {
    var k = uid % 4, hl = {}; hl[k] = 'ok';
    rec.step(1, 'user_id = ' + uid + '，' + uid + ' % 4 = ' + k + '：只需要访问分片 ' + k + '，其他 3 个分片完全不参与。', { hl: hl, vars: { user_id: uid, '分片': k } });
  }

  function shardPage(page, rec) {
    var need = page * 10, hl = { 0: 'cmp', 1: 'cmp', 2: 'cmp', 3: 'cmp' };
    var ret = SHARD_ROWS.map(function (n) { return Math.min(n, need); });
    var total = ret.reduce(function (a, b) { return a + b; }, 0);
    rec.step(3, '没有分片键：不知道第 ' + page + ' 页的 10 条落在哪个分片，所有的分片都要查。', { hl: hl, vars: { '页码': page } });
    rec.step(4, '每个分片都必须返回自己的前 ' + need.toLocaleString() + ' 条（最坏情况下第 ' + page + ' 页全部来自同一个分片）。', { hl: hl, ret: ret, vars: { '页码': page } });
    rec.step(5, '合并 ' + total.toLocaleString() + ' 行，排序，跳过前 ' + (need - 10).toLocaleString() + ' 条，取 10 条。' +
      (page >= 100000 ? '实测第 10 万页：合并 366 万行、101 MB。' : page >= 10000 ? '实测第 1 万页：合并 40 万行、11 MB。' : '') + '只能「下一页」时，用游标分页，每个分片只取 10 条。', { hl: hl, ret: ret, total: total, vars: { '页码': page } });
  }

  DSViz.register('shard-query', {
    title: '分片查询：带分片键访问一个分片；深分页要每个分片返回 10N 行',
    height: 235,
    width: 700,
    code: [
      'shard = user_id % 4;            // 带分片键：路由到一个分片',
      '',
      'for (每个分片)                   // 不带分片键：问所有分片',
      '    取 ORDER BY created_at DESC LIMIT 10 × 页码;',
      '合并、排序，取最后 10 条;'
    ],
    init: function () { return {}; },
    view: shardView,
    start: function (m, rec) { shardRoute(42, rec); },
    actions: [
      { label: '按用户查', fields: [{ key: 'u', label: 'user_id', value: 42, size: 6 }],
        check: function (m, p) { if (typeof p.u !== 'number' || p.u < 0) return '请输入非负整数'; },
        run: function (m, p, rec) { shardRoute(p.u, rec); } },
      { label: '全部订单第 N 页', fields: [{ key: 'n', label: '页码', value: 100000, size: 7 }],
        check: function (m, p) { if (typeof p.n !== 'number' || p.n < 1 || p.n > 400000) return '请输入 1～400000'; },
        run: function (m, p, rec) { shardPage(p.n, rec); } }
    ]
  });
})();
