/* 《单调栈与单调队列》：下一个更大的元素、滑动窗口最大值 */
(function () {
  'use strict';

  function parseList(s) {
    var parts = String(s).split(/[\s,，]+/).filter(Boolean).map(Number);
    return parts.every(Number.isInteger) ? parts : null;
  }
  function row(vals, x) {
    return {
      kind: 'array', x0: 20, cellW: x.w || 46, top: x.top, label: x.label, ptrs: x.ptrs || [], noIndex: x.noIndex,
      cells: vals.map(function (v, i) { return { v: v, hl: (x.hl || {})[i] || (x.dim && x.dim(i) ? 'dim' : null) }; })
    };
  }

  // ---------- 下一个更大的元素 ----------
  DSViz.register('next-greater', {
    title: 'nextGreater：用单调栈求右边第一个更大的元素',
    height: 290,
    code: [
      'for (int i = 0; i < n; ++i) {',
      '    while (top > 0 && a[stack[top - 1]] < a[i]) {',
      '        ans[stack[--top]] = i;',
      '    }',
      '    stack[top++] = i;',
      '}',
      'while (top > 0) ans[stack[--top]] = -1;'
    ],
    init: function () { return { a: [73, 74, 75, 71, 69, 72, 76, 73] }; },
    view: function (m, x) {
      var st = (x.stack || []).map(function (i) { return { v: i + ':' + m.a[i] }; });
      if (st.length && x.topHl) st[st.length - 1].hl = x.topHl;
      return {
        kind: 'multi', vars: x.vars || {},
        parts: [
          row(m.a, { top: 70, label: '输入 a（每天的温度）', hl: x.hl, ptrs: x.i != null ? [{ name: 'i', at: x.i }] : [] }),
          row((x.ans || []).map(function (v) { return v == null ? '' : v; }), { top: 200, label: 'ans：右边第一个更大的元素的下标', hl: x.ansHl }),
          { kind: 'array', vertical: true, x0: 580, top: 50, cap: 5, cellW: 70, noIndex: true, label: '栈（下标:值）', cells: st, ptrs: st.length ? [{ name: '栈顶', at: st.length - 1 }] : [] }
        ]
      };
    },
    start: function (m, rec) { rec.step(0, '对每一天，求之后第一个更高温度出现在哪一天。栈中存放「还没有找到答案」的下标，它们对应的温度从栈底到栈顶是递减的。', { ans: m.a.map(function () { return null; }) }); },
    actions: [{
      label: '开始',
      fields: [{ key: 'list', label: '数组', value: '73 74 75 71 69 72 76 73', size: 22 }],
      check: function (m, p) {
        var a = parseList(p.list);
        if (!a || !a.length) return '请输入用空格分隔的整数';
        return a.length > 10 ? '最多 10 个数' : null;
      },
      run: function (m, p, rec) {
        m.a = parseList(p.list);
        var n = m.a.length, st = [], ans = m.a.map(function () { return null; });
        function s(line, msg, i, extra) {
          extra = extra || {};
          var hl = {}; if (i != null && i < n) hl[i] = 'cur';
          rec.step(line, msg, { i: i, hl: hl, stack: st.slice(), ans: ans.slice(), ansHl: extra.ansHl, topHl: extra.topHl, vars: { i: i == null ? '-' : i, top: st.length } });
        }
        for (var i = 0; i < n; i++) {
          s(1, '看第 ' + i + ' 个数 a[' + i + '] = ' + m.a[i] + '。', i, { topHl: st.length ? 'cmp' : null });
          while (st.length && m.a[st[st.length - 1]] < m.a[i]) {
            var j = st.pop();
            ans[j] = i;
            var ah = {}; ah[j] = 'ok';
            s(3, '栈顶 a[' + j + '] = ' + m.a[j] + ' < ' + m.a[i] + '：a[' + i + '] 就是它在等的「右边第一个更大的数」。记录 ans[' + j + '] = ' + i + '，弹出。', i, { ansHl: ah, topHl: st.length ? 'cmp' : null });
          }
          st.push(i);
          s(5, '把 ' + i + ' 入栈，等待它右边更大的数。栈中的值仍然是递减的。', i, { topHl: 'new' });
        }
        while (st.length) { var k = st.pop(); ans[k] = -1; }
        s(7, '扫描结束，栈中剩下的元素右边没有更大的数，ans 为 -1。每个下标只入栈一次、出栈一次，总共 O(n)。', null);
      }
    }]
  });

  // ---------- 滑动窗口最大值 ----------
  DSViz.register('window-max', {
    title: 'windowMax：用单调队列求滑动窗口的最大值',
    height: 350,
    code: [
      'for (int i = 0; i < n; ++i) {',
      '    while (tail > head && a[dq[tail - 1]] <= a[i]) {',
      '        --tail;',
      '    }',
      '    dq[tail++] = i;',
      '    if (dq[head] <= i - k) {',
      '        ++head;',
      '    }',
      '    if (i >= k - 1) {',
      '        out[i - k + 1] = a[dq[head]];',
      '    }',
      '}'
    ],
    init: function () { return { a: [1, 3, -1, -3, 5, 3, 6, 7], k: 3 }; },
    view: function (m, x) {
      var lo = x.i == null ? -1 : Math.max(0, x.i - m.k + 1), hi = x.i == null ? -1 : x.i;
      var dq = (x.dq || []).map(function (i) { return i + ':' + m.a[i]; });
      var dqHl = {};
      if (x.dqHl) Object.keys(x.dqHl).forEach(function (k) { dqHl[k] = x.dqHl[k]; });
      return {
        kind: 'multi', vars: x.vars || {},
        parts: [
          row(m.a, { top: 70, label: '输入 a，窗口大小 k = ' + m.k + '（不在窗口中的变暗）', hl: x.hl, dim: function (j) { return x.i != null && (j < lo || j > hi); }, ptrs: x.i != null ? [{ name: 'i', at: x.i }] : [] }),
          row(dq, { top: 196, w: 62, label: '双端队列 dq（下标:值），从队头到队尾递减', hl: dqHl, noIndex: true, ptrs: dq.length ? [{ name: 'head', at: 0 }] : [] }),
          row(x.out || [], { top: 300, label: 'out：每个窗口的最大值', hl: x.outHl, noIndex: true })
        ]
      };
    },
    start: function (m, rec) { rec.step(0, '窗口从左向右滑动，每次移动一格。队列中只保留「将来有可能成为最大值」的下标，队头就是当前窗口的最大值。'); },
    actions: [{
      label: '开始',
      fields: [{ key: 'list', label: '数组', value: '1 3 -1 -3 5 3 6 7', size: 20 }, { key: 'k', label: 'k', value: 3, size: 2 }],
      check: function (m, p) {
        var a = parseList(p.list);
        if (!a || !a.length) return '请输入用空格分隔的整数';
        if (a.length > 10) return '最多 10 个数';
        if (!Number.isInteger(p.k) || p.k < 1 || p.k > a.length) return 'k 的范围是 1 到数组长度';
        return null;
      },
      run: function (m, p, rec) {
        m.a = parseList(p.list); m.k = p.k;
        var dq = [], out = [];
        function s(line, msg, i, extra) {
          extra = extra || {};
          var hl = {}; if (i != null) hl[i] = 'cur';
          rec.step(line, msg, { i: i, hl: hl, dq: dq.slice(), dqHl: extra.dqHl, out: out.slice(), outHl: extra.outHl, vars: { i: i, '窗口': '[' + Math.max(0, i - m.k + 1) + ', ' + i + ']' } });
        }
        for (var i = 0; i < m.a.length; i++) {
          s(1, 'a[' + i + '] = ' + m.a[i] + ' 进入窗口。', i, { dqHl: dq.length ? (function () { var o = {}; o[dq.length - 1] = 'cmp'; return o; })() : null });
          while (dq.length && m.a[dq[dq.length - 1]] <= m.a[i]) {
            var j = dq.pop();
            s(3, '队尾 a[' + j + '] = ' + m.a[j] + ' <= ' + m.a[i] + '：它比 a[' + i + '] 小，而且比 a[' + i + '] 更早离开窗口，以后永远不可能成为最大值了，从队尾删除。', i);
          }
          dq.push(i);
          s(5, '把下标 ' + i + ' 加到队尾。', i, { dqHl: (function () { var o = {}; o[dq.length - 1] = 'new'; return o; })() });
          if (dq[0] <= i - m.k) {
            var h = dq[0];
            s(6, '队头下标 ' + h + ' <= i - k = ' + (i - m.k) + '，已经滑出了窗口。', i, { dqHl: { 0: 'del' } });
            dq.shift();
            s(7, '从队头删除。', i);
          }
          if (i >= m.k - 1) {
            out.push(m.a[dq[0]]);
            var oh = {}; oh[out.length - 1] = 'ok';
            s(10, '窗口已经完整，队头 a[' + dq[0] + '] = ' + m.a[dq[0]] + ' 就是这个窗口的最大值。', i, { dqHl: { 0: 'ok' }, outHl: oh });
          }
        }
        s(12, '完成。每个下标最多进队一次、出队一次，总共 O(n)，和窗口的大小 k 无关。', m.a.length - 1);
      }
    }]
  });
})();
