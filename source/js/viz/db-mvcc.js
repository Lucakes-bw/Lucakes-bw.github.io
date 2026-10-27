/* 《隔离级别与 MVCC》：沿版本链查找当前事务能看到的版本 */
(function () {
  'use strict';
  // 张三这一行的三个版本：最新的在最左边，roll_ptr 指向更旧的版本
  var VERSIONS = [
    { id: 'v3', trx: 201, bal: 40, note: 'DB_TRX_ID 201，未提交' },
    { id: 'v2', trx: 200, bal: 70, note: 'DB_TRX_ID 200，已提交' },
    { id: 'v1', trx: 100, bal: 100, note: 'DB_TRX_ID 100，早已提交' }
  ];
  var X0 = 10, W = 180, GAP = 70, Y = 50;

  function view(m, x) {
    x = x || {};
    var hl = x.hl || {}, boxes = [], arrows = [], texts = [];
    VERSIONS.forEach(function (v, i) {
      boxes.push({ id: v.id, x: X0 + i * (W + GAP), y: Y, w: W, h: 46,
        text: 'balance = ' + v.bal, sub: v.note, hl: hl[v.id] });
      if (i > 0) arrows.push({ from: VERSIONS[i - 1].id, to: v.id, label: 'roll_ptr', hl: x.arrow === v.id ? 'cur' : null });
    });
    texts.push({ id: 'h1', x: X0, y: 36, text: '聚簇索引中的当前版本', cls: 'muted' });
    texts.push({ id: 'h2', x: X0 + W + GAP, y: 36, text: 'undo 日志中的旧版本 →', cls: 'muted' });
    if (m.rv) {
      boxes.push({ id: 'rv', x: X0, y: 170, w: 680, h: 40, text: m.rv, hl: x.rvHl });
      texts.push({ id: 'h3', x: X0, y: 158, text: m.rvTitle, cls: 'muted' });
    }
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts, vars: x.vars };
  }

  function walk(m, rec, ids, minId, maxId) {
    for (var i = 0; i < VERSIONS.length; i++) {
      var v = VERSIONS[i], h = {}, why, ok;
      h[v.id] = 'cur';
      if (v.trx < minId) { ok = true; why = v.trx + ' < ' + minId + '（最小的活跃事务），在建立读视图之前就提交了：可见'; }
      else if (v.trx >= maxId) { ok = false; why = v.trx + ' ≥ ' + maxId + '，是建立读视图之后才开始的事务：不可见'; }
      else if (ids.indexOf(v.trx) >= 0) { ok = false; why = v.trx + ' 在活跃事务列表中，建立读视图时还没有提交：不可见'; }
      else { ok = true; why = v.trx + ' 不在活跃列表中，已经提交：可见'; }
      if (ok) {
        h[v.id] = 'new';
        rec.step(0, '检查 balance = ' + v.bal + ' 的版本，DB_TRX_ID = ' + why + '。返回 ' + v.bal + '。',
          { hl: h, arrow: i > 0 ? v.id : null, vars: { '结果': v.bal } });
        return;
      }
      h[v.id] = 'del';
      rec.step(0, '检查 balance = ' + v.bal + ' 的版本，DB_TRX_ID = ' + why + '。沿 roll_ptr 找更旧的版本。',
        { hl: h, arrow: i > 0 ? v.id : null });
    }
  }

  DSViz.register('mvcc-read', {
    title: 'MVCC：沿版本链找可见的版本',
    height: 230,
    init: function () { return { rv: null, rvTitle: '' }; },
    view: view,
    start: function (m, rec) {
      rec.step(0, '张三这一行有三个版本：事务 100 插入了 100，事务 200 改成 70 并已提交，事务 201 又改成 40 但还没有提交。点按钮看两种隔离级别下读到的是哪个版本。');
    },
    actions: [
      { label: '可重复读', run: function (m, p, rec) {
        m.rvTitle = '会话 B 的读视图：在事务 200 提交之前、第一次读的时候建立，整个事务都用它';
        m.rv = '活跃事务 [200]    最小活跃 200    下一个事务编号 201';
        rec.step(0, '会话 B 在可重复读下，第一次读时建立了读视图，当时事务 200 正在进行。之后 200 提交、201 开始，都不改变这个读视图。', { rvHl: 'cur' });
        walk(m, rec, [200], 200, 201);
      } },
      { label: '读已提交', run: function (m, p, rec) {
        m.rvTitle = '会话 B 的读视图：读已提交下，每条语句都重新建立';
        m.rv = '活跃事务 [201]    最小活跃 201    下一个事务编号 202';
        rec.step(0, '读已提交下，这条 SELECT 执行时重新建立读视图：事务 200 已经提交，只有 201 还在进行。', { rvHl: 'cur' });
        walk(m, rec, [201], 201, 202);
      } }
    ]
  });
})();
