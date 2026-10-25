/* 《增删改查》：一条 SELECT 语句按什么顺序执行 */
(function () {
  'use strict';
  var SX = 10, SW = 110, RX = 150, RW = 300, TOP = 34, ROW = 28;
  var STAGES = ['FROM', 'WHERE', 'GROUP BY', 'HAVING', 'SELECT', 'ORDER BY'];

  var PRODUCTS = [
    [1, '机械键盘', '外设', 50], [2, '无线鼠标', '外设', 200], [3, '显示器', '显示', 15],
    [4, '便携屏', '显示', 0], [5, 'USB 集线器', '配件', 120], [6, 'Type-C 线', '配件', 500],
    [7, '笔记本支架', '配件', 35], [8, '降噪耳机', '音频', 8], [9, '蓝牙音箱', '音频', 0],
    [10, '新品摄像头', '外设', 0]
  ];

  // items: [{id, text, y, hl}]
  function view(m) {
    var boxes = STAGES.map(function (s, i) {
      return { id: 's' + i, x: SX, y: TOP + i * 40, w: SW, h: 30, text: s,
        hl: i === m.stage ? 'cur' : (i < m.stage ? null : 'empty') };
    });
    m.items.forEach(function (it) {
      boxes.push({ id: it.id, x: RX, y: it.y, w: RW, h: 24, text: it.text, hl: it.hl });
    });
    var texts = [{ id: 'h', x: RX, y: 20, text: m.head, cls: 'muted' }];
    (m.notes || []).forEach(function (n, i) { texts.push({ id: 'n' + i, x: RX + RW + 14, y: n.y + 16, text: n.text, cls: 'muted' }); });
    return { kind: 'free', boxes: boxes, arrows: [], texts: texts };
  }

  function rowText(p) { return p[1] + ' · ' + p[2] + ' · 库存 ' + p[3]; }
  function stack(items) { items.forEach(function (it, i) { it.y = TOP + i * ROW; }); }

  DSViz.register('sql-order', {
    title: 'SELECT 的执行顺序',
    height: 340,
    init: function () {
      var items = PRODUCTS.map(function (p) { return { id: 'r' + p[0], text: rowText(p), hl: null }; });
      stack(items);
      return { stage: -1, items: items, notes: [], head: 'product 表' };
    },
    view: view,
    start: function (m, rec) {
      rec.step(0, '查询：SELECT category, COUNT(*) AS n FROM product WHERE stock > 0 GROUP BY category HAVING n >= 2 ORDER BY n DESC。点「运行」，看它按什么顺序执行。');
    },
    actions: [{
      label: '运行',
      run: function (m, p, rec) {
        var all = PRODUCTS.map(function (p) { return { id: 'r' + p[0], text: rowText(p), hl: null, p: p }; });
        stack(all);
        m.items = all; m.notes = []; m.head = 'product 表';
        m.stage = 0;
        rec.step(0, 'FROM product：先确定数据从哪里来，得到表中的全部 10 行。');

        m.stage = 1;
        m.items.forEach(function (it) { it.hl = it.p[3] > 0 ? 'new' : 'del'; });
        rec.step(0, 'WHERE stock > 0：逐行检查条件，库存为 0 的 3 行（便携屏、蓝牙音箱、新品摄像头）不满足，被过滤掉。WHERE 作用于单独的行，这时还没有分组。');

        m.items = m.items.filter(function (it) { return it.p[3] > 0; });
        m.items.forEach(function (it) { it.hl = null; });
        stack(m.items);
        m.head = '剩下 7 行';
        rec.step(0, '剩下 7 行进入下一步。');

        m.stage = 2;
        var order = ['外设', '显示', '配件', '音频'], y = TOP, groups = [];
        m.notes = [];
        order.forEach(function (c) {
          var g = m.items.filter(function (it) { return it.p[2] === c; });
          m.notes.push({ y: y, text: '组：' + c + '（' + g.length + ' 行）' });
          g.forEach(function (it) { it.y = y; y += ROW; });
          groups.push({ c: c, n: g.length });
          y += 8;
        });
        m.items.sort(function (a, b) { return a.y - b.y; });
        m.head = '按 category 分组';
        rec.step(0, 'GROUP BY category：把 category 相同的行放进同一组，得到 4 组。');

        m.items = groups.map(function (g) { return { id: 'g' + g.c, text: g.c + ' · COUNT(*) = ' + g.n, hl: null, n: g.n, c: g.c }; });
        stack(m.items);
        m.notes = [];
        m.head = '每组变成一行';
        rec.step(0, '每一组计算聚合函数 COUNT(*)，然后缩成一行。从这一步开始，结果中的每一行代表一组，不再是单个商品，所以 SELECT 中只能出现分组的列和聚合函数。');

        m.stage = 3;
        m.items.forEach(function (it) { it.hl = it.n >= 2 ? 'new' : 'del'; });
        rec.step(0, 'HAVING n >= 2：过滤的是组。显示和音频两组只有 1 行，被过滤掉。WHERE 中不能用 COUNT(*)，因为执行 WHERE 时还没有分组；HAVING 在分组之后执行，可以用聚合函数。');

        m.items = m.items.filter(function (it) { return it.n >= 2; });
        m.items.forEach(function (it) { it.hl = null; });
        stack(m.items);
        rec.step(0, '剩下 2 组。');

        m.stage = 4;
        m.items.forEach(function (it) { it.text = 'category = ' + it.c + ' · n = ' + it.n; });
        m.head = '结果的列：category, n';
        rec.step(0, 'SELECT category, COUNT(*) AS n：计算要输出的列，并起别名 n。注意 SELECT 在 WHERE 之后执行，所以 WHERE 中不能使用别名；MySQL 允许在 HAVING 和 ORDER BY 中使用。');

        m.stage = 5;
        m.items.sort(function (a, b) { return b.n - a.n; });
        stack(m.items);
        m.items[0].hl = 'new';
        rec.step(0, 'ORDER BY n DESC：按 n 从大到小排序，配件（3）排到了外设（2）前面。如果有 LIMIT，最后才取前几行。');

        m.stage = 6;
        m.items.forEach(function (it) { it.hl = null; });
        m.head = '最终结果（2 行）';
        rec.step(0, '完成。书写顺序是 SELECT … FROM … WHERE … GROUP BY … HAVING … ORDER BY … LIMIT，逻辑上的执行顺序却是 FROM → WHERE → GROUP BY → HAVING → SELECT → ORDER BY → LIMIT。');
      }
    }]
  });
})();
