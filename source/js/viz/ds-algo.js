/* 《算法设计》一章的逐步演示
 *
 * algo-queens    回溯：4 皇后，冲突就剪枝、走不通就退回
 * algo-coin      动态规划：零钱兑换的一维表
 * algo-knapsack  动态规划：0-1 背包的二维表
 * algo-lis       最长递增子序列：tails 数组和二分查找
 * algo-greedy    贪心：按结束时间选活动
 * algo-window    滑动窗口：无重复字符的最长子串
 * algo-kmp       KMP：失配时模式按 next 数组跳
 */
(function () {
  'use strict';

  function box(id, x, y, w, h, text, hl, sub) { return { id: id, x: x, y: y, w: w, h: h, text: text, hl: hl || null, sub: sub }; }
  function txt(id, x, y, text, cls, anchor) { return { id: id, x: x, y: y, text: text, cls: cls || null, anchor: anchor || 'start' }; }

  /* ---------- 4 皇后 ---------- */

  var QN = 4, QS = 52, QX = 60, QY = 40;

  function queensView(m, x) {
    var boxes = [], texts = [];
    for (var r = 0; r < QN; r++) {
      texts.push(txt('r' + r, QX - 12, QY + r * QS + QS / 2 + 5, '行 ' + r, 'muted', 'end'));
      for (var c = 0; c < QN; c++) {
        var hl = null, t = '';
        if (x.cols[r] === c) { t = '♛'; hl = r === x.row ? 'cur' : 'ok'; }
        if (x.bad && x.bad.r === r && x.bad.c === c) { hl = 'warn'; t = '×'; }
        if (x.attackers && x.attackers.indexOf(r) >= 0 && x.cols[r] === c) hl = 'cmp';
        boxes.push(box('q' + r + '-' + c, QX + c * QS, QY + r * QS, QS - 4, QS - 4, t, hl));
      }
    }
    for (var c2 = 0; c2 < QN; c2++) texts.push(txt('c' + c2, QX + c2 * QS + QS / 2 - 2, QY - 10, '列 ' + c2, 'muted', 'middle'));
    texts.push(txt('cnt', 300, 60, '访问的结点：' + x.nodes, null));
    texts.push(txt('cols', 300, 90, 'queenCol = [' + x.cols.map(function (c) { return c < 0 ? '-' : c; }).join(', ') + ']', 'muted'));
    return { kind: 'free', boxes: boxes, texts: texts, vars: x.vars || {} };
  }

  function queensRun(m, p, rec) {
    var cols = [-1, -1, -1, -1], nodes = 0, done = false;
    function snap(line, msg, extra) {
      rec.step(line, msg, Object.assign({ cols: cols.slice(), nodes: nodes }, extra || {}));
    }
    function conflictWith(row, c) {
      var who = [];
      for (var r = 0; r < row; r++) if (cols[r] === c || Math.abs(cols[r] - c) === row - r) who.push(r);
      return who;
    }
    function solve(row) {
      nodes++;
      if (row === QN) {
        done = true;
        snap(3, '4 行都放好了，而且每一步都检查过冲突：找到了第一个解。这一路访问了 ' + nodes + ' 个结点。', { row: -1 });
        return;
      }
      for (var c = 0; c < QN && !done; c++) {
        var who = conflictWith(row, c);
        if (who.length) {
          var why = who.map(function (r) { return '第 ' + r + ' 行的皇后' + (cols[r] === c ? '在同一列' : '在同一条斜线上'); }).join('，');
          snap(6, '第 ' + row + ' 行试第 ' + c + ' 列：和' + why + '，剪掉，下面的整棵子树都不用走了。',
            { row: row, bad: { r: row, c: c }, attackers: who, vars: { row: row, c: c } });
          continue;
        }
        cols[row] = c;
        snap(7, '第 ' + row + ' 行放在第 ' + c + ' 列，和前面的都不冲突，进入第 ' + (row + 1) + ' 行。', { row: row, vars: { row: row, c: c } });
        solve(row + 1);
        if (done) return;
        cols[row] = -1;
        snap(8, '第 ' + (row + 1) + ' 行没有任何安全的位置，回到第 ' + row + ' 行，撤销第 ' + c + ' 列的皇后，试下一列。', { row: row, vars: { row: row, c: c } });
      }
    }
    snap(1, '从第 0 行开始，每一行选一列放皇后。', { row: 0 });
    solve(0);
  }

  DSViz.register('algo-queens', {
    title: '回溯：4 皇后',
    height: 260,
    code: [
      'long long queensPrune(int n, int row)',
      '{',
      '    if (row == n) return 1;',
      '    long long total = 0;',
      '    for (int c = 0; c < n; ++c) {',
      '        if (!safe(row, c)) continue;      // 剪枝',
      '        queenCol[row] = c;',
      '        total += queensPrune(n, row + 1);',
      '    }',
      '    return total;',
      '}'
    ],
    init: function () { return {}; },
    view: queensView,
    start: function (m, rec) { queensRun(m, {}, rec); },
    actions: [{ label: '重新开始', run: queensRun }]
  });

  /* ---------- 零钱兑换 ---------- */

  var COINS = [1, 3, 4], AMOUNT = 6, CW = 64;

  function coinView(m, x) {
    var boxes = [], texts = [];
    texts.push(txt('t', 20, 30, '硬币 {1, 3, 4}，dp[x] = 凑出金额 x 最少要几枚', 'muted'));
    for (var i = 0; i <= AMOUNT; i++) {
      var v = x.dp[i];
      boxes.push(box('d' + i, 40 + i * CW, 70, CW - 6, 44, v === undefined ? '' : (v === Infinity ? '∞' : String(v)), (x.hl || {})[i], 'x=' + i));
    }
    (x.notes || []).forEach(function (n, k) { texts.push(txt('n' + k, 40, 160 + k * 22, n, k === 0 ? null : 'muted')); });
    return { kind: 'free', boxes: boxes, texts: texts, vars: x.vars || {} };
  }

  function coinRun(m, p, rec) {
    var dp = [0];
    rec.step(1, 'dp[0] = 0：凑出 0 元不需要硬币。从小到大计算每个金额。', { dp: dp.slice(), hl: { 0: 'ok' } });
    for (var x = 1; x <= AMOUNT; x++) {
      var best = Infinity, from = -1, hl = {}, notes = [];
      COINS.forEach(function (c) {
        if (c <= x) {
          hl[x - c] = 'cmp';
          notes.push('最后一枚是 ' + c + '：dp[' + (x - c) + '] + 1 = ' + (dp[x - c] + 1));
          if (dp[x - c] + 1 < best) { best = dp[x - c] + 1; from = x - c; }
        }
      });
      hl[x] = 'cur';
      rec.step(4, '算 dp[' + x + ']：最后一枚硬币可能是 ' + COINS.filter(function (c) { return c <= x; }).join('、') + '，看黄色的几格。', { dp: dp.slice(), hl: hl, notes: notes, vars: { x: x } });
      dp[x] = best;
      var hl2 = {}; hl2[from] = 'ok'; hl2[x] = 'new';
      rec.step(6, 'dp[' + x + '] = dp[' + from + '] + 1 = ' + best + (x === 6 ? '。贪心会先拿 4，得到 4+1+1 三枚；DP 找到了 3+3 两枚。' : '。'), { dp: dp.slice(), hl: hl2, notes: notes, vars: { x: x } });
    }
  }

  DSViz.register('algo-coin', {
    title: '零钱兑换：一格一格地填表',
    height: 240,
    code: [
      'dp[0] = 0;',
      'for (int x = 1; x <= amount; ++x) {',
      '    dp[x] = INT_MAX;',
      '    for (每种硬币 c)',
      '        if (c <= x && dp[x - c] + 1 < dp[x])',
      '            dp[x] = dp[x - c] + 1;',
      '}'
    ],
    init: function () { return {}; },
    view: coinView,
    start: function (m, rec) { coinRun(m, {}, rec); },
    actions: [{ label: '重新开始', run: coinRun }]
  });

  /* ---------- 0-1 背包 ---------- */

  var KW = [1, 3, 4, 5], KV = [15, 20, 30, 55], KCAP = 8, KX = 150, KY = 40, KC = 52, KR = 40;

  function knapView(m, x) {
    var boxes = [], texts = [];
    for (var w = 0; w <= KCAP; w++) texts.push(txt('w' + w, KX + w * KC + KC / 2 - 3, KY - 8, String(w), 'muted', 'middle'));
    texts.push(txt('wl', KX - 10, KY - 8, '容量 →', 'muted', 'end'));
    for (var i = 0; i <= 4; i++) {
      texts.push(txt('l' + i, KX - 10, KY + i * KR + 25, i === 0 ? '（无）' : '物品' + i + ' 重' + KW[i - 1] + ' 值' + KV[i - 1], 'muted', 'end'));
      for (var w2 = 0; w2 <= KCAP; w2++) {
        var v = x.dp[i][w2];
        boxes.push(box('k' + i + '-' + w2, KX + w2 * KC, KY + i * KR, KC - 4, KR - 4, v === undefined ? '' : String(v), (x.hl || {})[i + '-' + w2]));
      }
    }
    return { kind: 'free', boxes: boxes, texts: texts, vars: x.vars || {} };
  }

  function knapRun(m, p, rec) {
    var dp = [];
    for (var i = 0; i <= 4; i++) dp.push([]);
    for (var w = 0; w <= KCAP; w++) dp[0][w] = 0;
    function copy() { return dp.map(function (r) { return r.slice(); }); }
    rec.step(1, '第 0 行：没有物品，价值都是 0。下面一行一行地填，每一格只看上一行。', { dp: copy() });
    for (i = 1; i <= 4; i++) {
      for (w = 0; w <= KCAP; w++) {
        var hl = {}, keep = dp[i - 1][w];
        hl[(i - 1) + '-' + w] = 'cmp';
        hl[i + '-' + w] = 'cur';
        var msg = 'dp[' + i + '][' + w + ']：不选物品' + i + ' = ' + keep;
        var val = keep;
        if (KW[i - 1] <= w) {
          var take = dp[i - 1][w - KW[i - 1]] + KV[i - 1];
          hl[(i - 1) + '-' + (w - KW[i - 1])] = 'cmp';
          msg += '；选它 = dp[' + (i - 1) + '][' + (w - KW[i - 1]) + '] + ' + KV[i - 1] + ' = ' + take;
          if (take > keep) val = take;
        } else {
          msg += '；放不下（重 ' + KW[i - 1] + '），只能不选';
        }
        dp[i][w] = val;
        msg += '。取 ' + val + '。';
        if (i === 4 && w === KCAP) msg += ' 右下角就是答案。';
        rec.step(KW[i - 1] <= w ? 5 : 3, msg, { dp: copy(), hl: hl, vars: { i: i, w: w } });
      }
    }
  }

  DSViz.register('algo-knapsack', {
    title: '0-1 背包：二维表',
    height: 250,
    width: 640,
    code: [
      'for (int i = 1; i <= n; ++i)',
      '    for (int w = 0; w <= W; ++w) {',
      '        dp[i][w] = dp[i-1][w];             // 不选',
      '        if (wt[i-1] <= w)',
      '            dp[i][w] = max(dp[i][w],',
      '                dp[i-1][w-wt[i-1]] + val[i-1]);   // 选',
      '    }'
    ],
    init: function () { return {}; },
    view: knapView,
    start: function (m, rec) { knapRun(m, {}, rec); },
    actions: [{ label: '重新开始', run: knapRun }]
  });

  /* ---------- 最长递增子序列 ---------- */

  var LA = [10, 9, 2, 5, 3, 7, 101, 18];

  function lisView(m, x) {
    var boxes = [], texts = [];
    texts.push(txt('a', 20, 34, '数组 a', 'muted'));
    LA.forEach(function (v, i) { boxes.push(box('a' + i, 90 + i * 64, 14, 58, 36, String(v), i === x.i ? 'cur' : (i < x.i ? 'dim' : null), String(i))); });
    texts.push(txt('t', 20, 124, 'tails', 'muted'));
    for (var k = 0; k < 6; k++) {
      var v = x.tails[k];
      var hl = k === x.pos ? x.posHl : (k >= x.tails.length ? 'empty' : ((x.range && k >= x.range[0] && k < x.range[1]) ? null : (x.range ? 'dim' : null)));
      if (x.mid === k) hl = 'cmp';
      boxes.push(box('t' + k, 90 + k * 64, 104, 58, 36, v === undefined ? '' : String(v), hl, '长度' + (k + 1)));
    }
    texts.push(txt('len', 520, 124, '长度 = ' + x.tails.length, null));
    return { kind: 'free', boxes: boxes, texts: texts, vars: x.vars || {} };
  }

  function lisRun(m, p, rec) {
    var tails = [];
    rec.step(1, 'tails[k] = 长度为 k+1 的递增子序列的最小结尾。tails 总是递增的，所以可以二分查找。', { i: -1, tails: [] });
    LA.forEach(function (x, i) {
      var lo = 0, hi = tails.length;
      rec.step(2, '处理 a[' + i + '] = ' + x + '：在 tails 中二分查找第一个 ≥ ' + x + ' 的位置。', { i: i, tails: tails.slice(), range: [lo, hi], vars: { x: x, lo: lo, hi: hi } });
      while (lo < hi) {
        var mid = (lo + hi) >> 1;
        var go = tails[mid] < x;
        rec.step(5, 'mid = ' + mid + '：tails[' + mid + '] = ' + tails[mid] + (go ? ' < ' + x + '，答案在右边，lo = mid + 1。' : ' ≥ ' + x + '，答案在 mid 或左边，hi = mid。'),
          { i: i, tails: tails.slice(), range: [lo, hi], mid: mid, vars: { x: x, lo: lo, hi: hi, mid: mid } });
        if (go) lo = mid + 1; else hi = mid;
      }
      var append = lo === tails.length;
      var old = tails[lo];
      tails[lo] = x;
      rec.step(8, append ? x + ' 比 tails 中所有的数都大：接在最后，最长的长度变成 ' + tails.length + '。'
                         : '把 tails[' + lo + '] 从 ' + old + ' 换成 ' + x + '：长度为 ' + (lo + 1) + ' 的递增子序列有了更小的结尾，后面的数更容易接上。',
        { i: i, tails: tails.slice(), pos: lo, posHl: append ? 'ok' : 'new', vars: { x: x, 位置: lo } });
    });
    rec.step(0, '处理完了，tails 的长度 ' + tails.length + ' 就是答案。注意 tails = [' + tails.join(', ') + '] 本身不一定是一个递增子序列。', { i: LA.length, tails: tails.slice() });
  }

  DSViz.register('algo-lis', {
    title: '最长递增子序列：tails 和二分查找',
    height: 175,
    code: [
      'for (int i = 0; i < n; ++i) {',
      '    int lo = 0, hi = len;',
      '    while (lo < hi) {',
      '        int mid = lo + (hi - lo) / 2;',
      '        if (tails[mid] < a[i]) lo = mid + 1;',
      '        else hi = mid;',
      '    }',
      '    tails[lo] = a[i];',
      '    if (lo == len) ++len;',
      '}'
    ],
    init: function () { return {}; },
    view: lisView,
    start: function (m, rec) { lisRun(m, {}, rec); },
    actions: [{ label: '重新开始', run: lisRun }]
  });

  /* ---------- 活动选择 ---------- */

  // 《算法导论》中的例子，已经按结束时间排好
  var ACTS = [[1, 4], [3, 5], [0, 6], [5, 7], [3, 9], [5, 9], [6, 10], [8, 11], [8, 12], [2, 14], [12, 16]];
  var GX = 70, GU = 34, GY = 30, GH = 20;

  function greedyView(m, x) {
    var boxes = [], texts = [];
    for (var t = 0; t <= 16; t += 2) texts.push(txt('t' + t, GX + t * GU, GY + ACTS.length * (GH + 4) + 16, String(t), 'muted', 'middle'));
    ACTS.forEach(function (a, i) {
      texts.push(txt('l' + i, GX - 8, GY + i * (GH + 4) + 15, 'a' + (i + 1), 'muted', 'end'));
      boxes.push(box('g' + i, GX + a[0] * GU, GY + i * (GH + 4), (a[1] - a[0]) * GU - 2, GH, '[' + a[0] + ',' + a[1] + ')', (x.hl || {})[i]));
    });
    if (x.lastEnd != null && x.lastEnd > -1) texts.push(txt('le', GX + x.lastEnd * GU, GY - 8, x.lastEnd >= 10 ? '上一个选中的结束于 ' + x.lastEnd + ' ↓' : '↓ 上一个选中的结束于 ' + x.lastEnd, 'muted', x.lastEnd >= 10 ? 'end' : 'start'));
    return { kind: 'free', boxes: boxes, texts: texts, vars: x.vars || {} };
  }

  function greedyRun(m, p, rec) {
    var hl = {}, lastEnd = -1, count = 0;
    rec.step(1, '11 个活动已经按结束时间排好。依次看每一个：开始时间不早于上一个选中的结束时间，就选它。', { hl: {}, lastEnd: -1 });
    ACTS.forEach(function (a, i) {
      var h = Object.assign({}, hl);
      h[i] = 'cur';
      if (a[0] >= lastEnd) {
        hl[i] = 'ok';
        lastEnd = a[1];
        count++;
        rec.step(4, 'a' + (i + 1) + ' = [' + a[0] + ',' + a[1] + ')：' + (count === 1 ? '还没有选任何活动，结束得最早的就是它' : '开始 ' + a[0] + ' ≥ 上一个的结束') + '，选它。已选 ' + count + ' 个。',
          { hl: Object.assign({}, hl), lastEnd: lastEnd, vars: { 已选: count } });
      } else {
        hl[i] = 'dim';
        rec.step(3, 'a' + (i + 1) + ' = [' + a[0] + ',' + a[1] + ')：开始 ' + a[0] + ' < ' + lastEnd + '，和已选的冲突，跳过。', { hl: h, lastEnd: lastEnd, vars: { 已选: count } });
      }
    });
    rec.step(0, '一共选了 ' + count + ' 个活动：a1、a4、a8、a11。这是最多的，正文第 2.3 节用交换论证说明了为什么。', { hl: hl, lastEnd: lastEnd, vars: { 已选: count } });
  }

  DSViz.register('algo-greedy', {
    title: '活动选择：按结束时间贪心',
    height: 320,
    width: 640,
    code: [
      'qsort(iv, n, sizeof(Interval), byEnd);',
      'for (int i = 0; i < n; ++i)',
      '    if (iv[i].start >= lastEnd) {',
      '        ++count;',
      '        lastEnd = iv[i].end;',
      '    }'
    ],
    init: function () { return {}; },
    view: greedyView,
    start: function (m, rec) { greedyRun(m, {}, rec); },
    actions: [{ label: '重新开始', run: greedyRun }]
  });

  /* ---------- 滑动窗口 ---------- */

  function windowView(m, x) {
    var boxes = [], texts = [], s = x.s;
    for (var i = 0; i < s.length; i++) {
      var hl = null;
      if (i >= x.left && i <= x.right) hl = 'cmp';
      if (i === x.right) hl = 'cur';
      if (x.bestRange && i >= x.bestRange[0] && i < x.bestRange[1] && x.final) hl = 'ok';
      boxes.push(box('s' + i, 30 + i * 46, 50, 40, 40, s[i], hl, String(i)));
    }
    if (x.left <= x.right) {
      texts.push(txt('L', 30 + x.left * 46 + 20, 40, 'left', 'muted', 'middle'));
      texts.push(txt('R', 30 + x.right * 46 + 20, 30, 'i', null, 'middle'));
    }
    texts.push(txt('last', 30, 140, 'last：' + Object.keys(x.last).map(function (k) { return k + '→' + x.last[k]; }).join('  '), 'muted'));
    return { kind: 'free', boxes: boxes, texts: texts, vars: x.vars || {} };
  }

  function windowRun(m, p, rec) {
    var s = String(p.s == null ? 'abcabcbb' : p.s).slice(0, 12);
    var last = {}, left = 0, best = 0, bestLeft = 0;
    rec.step(1, '窗口 [left, i] 中没有重复的字符。i 每次右移一格；新字符在窗口中出现过，left 就跳到它上一次出现的后面。', { s: s, left: 0, right: -1, last: {} });
    for (var i = 0; i < s.length; i++) {
      var c = s[i], msg;
      if (last[c] !== undefined && last[c] >= left) {
        msg = '「' + c + '」上一次出现在 ' + last[c] + '，在窗口中：left 从 ' + left + ' 跳到 ' + (last[c] + 1) + '。';
        left = last[c] + 1;
      } else if (last[c] !== undefined) {
        msg = '「' + c + '」上一次出现在 ' + last[c] + '，但那已经在窗口外面了（< left），left 不动。';
      } else {
        msg = '「' + c + '」第一次出现，窗口直接变长。';
      }
      last[c] = i;
      if (i - left + 1 > best) { best = i - left + 1; bestLeft = left; msg += '窗口长度 ' + best + '，是目前最长的。'; }
      var lastCopy = {}; Object.keys(last).forEach(function (k) { lastCopy[k] = last[k]; });
      rec.step(3, 'i = ' + i + '：' + msg, { s: s, left: left, right: i, last: lastCopy, vars: { left: left, i: i, best: best } });
    }
    rec.step(0, '最长的无重复子串是「' + s.substr(bestLeft, best) + '」，长度 ' + best + '。left 和 i 都只向右走，一共 O(n) 步。',
      { s: s, left: 1, right: 0, last: last, final: true, bestRange: [bestLeft, bestLeft + best], vars: { best: best } });
  }

  DSViz.register('algo-window', {
    title: '滑动窗口：无重复字符的最长子串',
    height: 160,
    code: [
      'for (int i = 0; s[i]; ++i) {',
      '    unsigned char c = s[i];',
      '    if (last[c] >= left) left = last[c] + 1;',
      '    last[c] = i;',
      '    best = max(best, i - left + 1);',
      '}'
    ],
    init: function () { return {}; },
    view: windowView,
    start: function (m, rec) { windowRun(m, { s: 'abcabcbb' }, rec); },
    actions: [{
      label: '运行',
      fields: [{ key: 's', label: '字符串（最多 12 个字符）', value: 'abba', size: 12 }],
      check: function (m, p) { return String(p.s).length ? null : '请输入字符串'; },
      run: windowRun
    }]
  });

  /* ---------- KMP ---------- */

  var KT = 'ABABDABACDABABCABAB', KP = 'ABABCABAB', KS = 32, KTX = 20;

  function nextOf(p) {
    var nx = [0];
    for (var i = 1, len = 0; i < p.length; i++) {
      while (len > 0 && p[i] !== p[len]) len = nx[len - 1];
      if (p[i] === p[len]) len++;
      nx[i] = len;
    }
    return nx;
  }
  var KNEXT = nextOf(KP);

  function kmpView(m, x) {
    var boxes = [], texts = [];
    texts.push(txt('tl', KTX, 22, 'text', 'muted'));
    for (var i = 0; i < KT.length; i++) {
      var hl = i === x.i ? (x.res === 'ok' ? 'ok' : x.res === 'bad' ? 'warn' : 'cur') : (i < x.i ? 'dim' : null);
      if (x.found != null && i >= x.found && i < x.found + KP.length) hl = 'ok';
      boxes.push(box('t' + i, KTX + i * KS, 30, KS - 4, 32, KT[i], hl, String(i)));
    }
    var off = x.i - x.j;
    if (x.found != null) off = x.found;
    texts.push(txt('pl', KTX, 108, 'pattern', 'muted'));
    for (var j = 0; j < KP.length; j++) {
      var h2 = j < x.j ? 'ok' : (j === x.j && x.res ? (x.res === 'ok' ? 'ok' : 'warn') : null);
      if (x.found != null) h2 = 'ok';
      boxes.push(box('p' + j, KTX + (off + j) * KS, 116, KS - 4, 32, KP[j], h2, String(j)));
    }
    texts.push(txt('nl', KTX, 186, 'next = [' + KNEXT.join(', ') + ']', 'muted'));
    return { kind: 'free', boxes: boxes, texts: texts, vars: x.vars || {} };
  }

  function kmpRun(m, p, rec) {
    var j = 0;
    rec.step(1, '在 text 中找 pattern。next 数组已经算好。i 是 text 中的位置，只往前走；j 是已经匹配了几个字符。', { i: 0, j: 0 });
    for (var i = 0; i < KT.length; i++) {
      while (j > 0 && KT[i] !== KP[j]) {
        rec.step(3, 'text[' + i + '] = ' + KT[i] + '，pattern[' + j + '] = ' + KP[j] + '：失配。前面已经匹配的 ' + j + ' 个字符中，最长相等的前后缀长 next[' + (j - 1) + '] = ' + KNEXT[j - 1] + '，模式右移，j 变成 ' + KNEXT[j - 1] + '，i 不动。',
          { i: i, j: j, res: 'bad', vars: { i: i, j: j } });
        j = KNEXT[j - 1];
      }
      if (KT[i] === KP[j]) {
        rec.step(4, 'text[' + i + '] = pattern[' + j + '] = ' + KT[i] + '：匹配，j 变成 ' + (j + 1) + '。', { i: i, j: j, res: 'ok', vars: { i: i, j: j } });
        j++;
      } else {
        rec.step(4, 'text[' + i + '] = ' + KT[i] + '，pattern[0] = ' + KP[0] + '：失配，j 已经是 0，i 前进。', { i: i, j: j, res: 'bad', vars: { i: i, j: j } });
      }
      if (j === KP.length) {
        rec.step(5, '匹配了全部 ' + KP.length + ' 个字符：在位置 ' + (i - KP.length + 1) + ' 找到了。i 从来没有回退过。', { i: i, j: j, found: i - KP.length + 1, vars: { 位置: i - KP.length + 1 } });
        return;
      }
    }
  }

  DSViz.register('algo-kmp', {
    title: 'KMP：失配时按 next 数组跳',
    height: 200,
    width: 640,
    code: [
      'for (int i = 0, j = 0; i < n; ++i) {',
      '    while (j > 0 && text[i] != pat[j])',
      '        j = next[j - 1];',
      '    if (text[i] == pat[j]) ++j;',
      '    if (j == m) return i - m + 1;',
      '}'
    ],
    init: function () { return {}; },
    view: kmpView,
    start: function (m, rec) { kmpRun(m, {}, rec); },
    actions: [{ label: '重新开始', run: kmpRun }]
  });
})();
