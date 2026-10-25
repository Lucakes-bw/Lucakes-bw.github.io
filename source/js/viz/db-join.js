/* 《多表查询》：内连接和左连接怎样把两个表的行配对 */
(function () {
  'use strict';
  var CX = 10, CW = 120, OX = 210, OW = 170, RX = 470, RW = 220, TOP = 40, ROW = 30;
  var CUSTOMERS = [[1, '李雷'], [2, '韩梅梅'], [3, '张三'], [4, '王芳'], [5, '赵六']];
  var ORDERS = [[1, 1], [2, 1], [3, 2], [4, 3], [5, 2], [6, 4]];

  function view(m) {
    var boxes = [], arrows = [];
    CUSTOMERS.forEach(function (c, i) {
      boxes.push({ id: 'c' + c[0], x: CX, y: TOP + i * ROW, w: CW, h: 24, text: c[0] + ' ' + c[1], hl: m.cur === c[0] ? 'cur' : null });
    });
    ORDERS.forEach(function (o, i) {
      var hl = m.match.indexOf(o[0]) >= 0 ? 'new' : (m.scan ? 'dim' : null);
      boxes.push({ id: 'o' + o[0], x: OX, y: TOP + i * ROW, w: OW, h: 24, text: '订单 ' + o[0] + ' · 客户 ' + o[1], hl: hl });
    });
    m.result.forEach(function (r, i) {
      boxes.push({ id: 'r' + r.id, x: RX, y: TOP + i * ROW, w: RW, h: 24, text: r.text, hl: r.hl });
    });
    m.match.forEach(function (oid) { arrows.push({ from: 'c' + m.cur, to: 'o' + oid, hl: 'cur' }); });
    return { kind: 'free', boxes: boxes, arrows: arrows,
      texts: [{ id: 'hc', x: CX, y: 26, text: 'customer c', cls: 'muted' }, { id: 'ho', x: OX, y: 26, text: 'orders o', cls: 'muted' },
        { id: 'hr', x: RX, y: 26, text: m.head, cls: 'muted' }] };
  }

  function join(m, rec, left) {
    m.result = []; m.cur = null; m.match = []; m.scan = false;
    m.head = (left ? 'LEFT JOIN' : 'JOIN') + ' 的结果';
    rec.step(0, (left ? 'customer c LEFT JOIN orders o' : 'customer c JOIN orders o') + ' ON o.customer_id = c.id：对 customer 的每一行，到 orders 中找出满足 ON 条件的行，配成结果中的一行。');
    CUSTOMERS.forEach(function (c) {
      m.cur = c[0]; m.scan = true;
      m.match = ORDERS.filter(function (o) { return o[1] === c[0]; }).map(function (o) { return o[0]; });
      m.result.forEach(function (r) { r.hl = null; });
      if (m.match.length) {
        m.match.forEach(function (oid) { m.result.push({ id: c[0] + '-' + oid, text: c[1] + ' · 订单 ' + oid, hl: 'new' }); });
        rec.step(0, c[1] + '（id = ' + c[0] + '）：orders 中 customer_id = ' + c[0] + ' 的有 ' + m.match.length + ' 行（订单 ' + m.match.join('、') + '），' +
          (m.match.length > 1 ? '这个客户在结果中出现了 ' + m.match.length + ' 次。' : '配成 1 行。'));
      } else if (left) {
        m.result.push({ id: c[0] + '-null', text: c[1] + ' · NULL', hl: 'warn' });
        rec.step(0, c[1] + '（id = ' + c[0] + '）：orders 中没有匹配的行。LEFT JOIN 保留左表的每一行，右表的列全部填 NULL。');
      } else {
        rec.step(0, c[1] + '（id = ' + c[0] + '）：orders 中没有匹配的行。内连接只保留配对成功的行，' + c[1] + '不出现在结果中。');
      }
    });
    m.cur = null; m.match = []; m.scan = false;
    m.result.forEach(function (r) { r.hl = r.text.indexOf('NULL') >= 0 ? 'warn' : null; });
    rec.step(0, left
      ? '完成，共 7 行：6 个配对，加上没有订单的赵六。要找「没有下过订单的客户」，就在这个结果上加 WHERE o.id IS NULL。'
      : '完成，共 6 行，每个订单一行。没有订单的赵六不在结果中；有两个订单的李雷、韩梅梅各出现两次。');
  }

  DSViz.register('sql-join', {
    title: '连接：行怎样配对',
    height: 270,
    init: function () { return { result: [], cur: null, match: [], scan: false, head: '结果' }; },
    view: view,
    start: function (m, rec) { rec.step(0, '分别点「内连接」和「左连接」，比较两种连接的结果。'); },
    actions: [
      { label: '内连接', run: function (m, p, rec) { join(m, rec, false); } },
      { label: '左连接', run: function (m, p, rec) { join(m, rec, true); } }
    ]
  });
})();
