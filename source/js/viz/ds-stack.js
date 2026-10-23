/* 《栈》：入栈出栈、括号匹配、中缀表达式转后缀表达式 */
(function () {
  'use strict';

  function stackPart(items, x) {
    var cells = items.map(function (v, i) { return { v: v, hl: i === items.length - 1 ? x.topHl : null }; });
    var ptrs = items.length ? [{ name: '栈顶', at: items.length - 1 }] : [];
    return { kind: 'array', vertical: true, cells: cells, cap: x.cap || 5, x0: x.x0 || 600, top: x.top || 22, cellW: 56, ptrs: ptrs, noIndex: true, label: x.label };
  }
  function strPart(chars, x) {
    var cells = chars.map(function (c, i) { return { v: c, hl: (x.hl || {})[i] || (x.done != null && i < x.done ? 'dim' : null) }; });
    return { kind: 'array', cells: cells, cellW: 34, top: x.top || 70, x0: 20, ptrs: x.ptrs || [], noIndex: true, label: x.label };
  }

  // ---------- 入栈、出栈 ----------
  DSViz.register('stack-ops', {
    title: '入栈和出栈：只能在栈顶操作',
    height: 240,
    code: [
      'int push(Stack* s, int value)',
      '{',
      '    if (s->top == s->capacity) { /* 扩容，同顺序表 */ }',
      '    s->data[s->top++] = value;',
      '    return 1;',
      '}',
      '',
      'int pop(Stack* s, int* out)',
      '{',
      '    if (s->top == 0) return 0;',
      '    *out = s->data[--s->top];',
      '    return 1;',
      '}'
    ],
    init: function () { return { items: [10, 20] }; },
    view: function (m, x) {
      var p = stackPart(m.items, { topHl: x.topHl, x0: 300, top: 24 });
      p.cap = 5;
      return Object.assign(p, { vars: { top: m.items.length } });
    },
    start: function (m, rec) { rec.step(0, '栈中有 10 和 20，20 在栈顶。试试入栈、出栈，观察每次操作的都是哪一个元素。'); },
    actions: [{
      label: '入栈 push',
      fields: [{ key: 'value', label: 'value', value: 30, size: 4 }],
      check: function (m, p) {
        if (!Number.isInteger(p.value)) return 'value 要是整数';
        return m.items.length >= 5 ? '演示区最多放 5 个元素（真正的栈会自动扩容）' : null;
      },
      run: function (m, p, rec) {
        rec.step(3, '检查空间：top = ' + m.items.length + '，空间足够（满了就像顺序表一样扩容）。');
        m.items.push(p.value);
        rec.step(4, 'data[top] = ' + p.value + '，然后 top 加 1。新元素放在最上面，成为新的栈顶。O(1)。', { topHl: 'new' });
      }
    }, {
      label: '出栈 pop',
      run: function (m, p, rec) {
        if (!m.items.length) { rec.step(10, '栈是空的（top == 0），pop 失败，返回 0。'); return; }
        rec.step(11, '先将 top 减 1，再取出 data[top]：出栈的是最后放进去的 ' + m.items[m.items.length - 1] + '。', { topHl: 'del' });
        m.items.pop();
        rec.step(12, '出栈完成。O(1)。');
      }
    }]
  });

  // ---------- 括号匹配 ----------
  var PAIR = { ')': '(', ']': '[', '}': '{' };
  DSViz.register('bracket', {
    title: 'isBalanced：用栈检查括号是否匹配',
    height: 250,
    code: [
      'for (const char* p = text; *p != \'\\0\' && ok; ++p) {',
      '    char c = *p;',
      '    if (c == \'(\' || c == \'[\' || c == \'{\') {',
      '        push(&s, c);',
      '    } else if (c == \')\' || c == \']\' || c == \'}\') {',
      '        int open;',
      '        if (!pop(&s, &open)) {',
      '            ok = 0;',
      '        } else if (/* open 和 c 不是一对 */) {',
      '            ok = 0;',
      '        }',
      '    }',
      '}',
      'if (!isEmpty(&s)) ok = 0;'
    ],
    init: function () { return { text: '{a[b(c)d]e}' }; },
    view: function (m, x) {
      return {
        kind: 'multi', vars: x.vars || {},
        parts: [strPart(m.text.split(''), { hl: x.hl, done: x.done, ptrs: x.p != null ? [{ name: 'p', at: x.p }] : [], top: 96, label: '输入' }),
          stackPart(x.stack || [], { topHl: x.topHl, x0: 600, top: 40, cap: 5, label: '栈' })]
      };
    },
    start: function (m, rec) { rec.step(0, '左括号入栈，等待配对；遇到右括号时，栈顶必须是和它同类型的左括号。试试 ([)] 或 (() 等不匹配的例子。'); },
    actions: [{
      label: '检查',
      fields: [{ key: 'text', label: '输入', value: '{a[b(c)d]e}', size: 14 }],
      check: function (m, p) { var t = String(p.text); return t.length > 14 ? '最多 14 个字符' : (t.length === 0 ? '请输入内容' : null); },
      run: function (m, p, rec) {
        m.text = String(p.text);
        var st = [], ok = true, i;
        for (i = 0; i < m.text.length && ok; i++) {
          var c = m.text[i], hl = {};
          hl[i] = 'cur';
          if ('([{'.indexOf(c) >= 0) {
            rec.step(3, "'" + c + "' 是左括号。", { p: i, hl: hl, done: i, stack: st.slice() });
            st.push(c);
            rec.step(4, '入栈，等待和它配对的右括号。', { p: i, hl: hl, done: i, stack: st.slice(), topHl: 'new' });
          } else if (PAIR[c]) {
            rec.step(5, "'" + c + "' 是右括号，取出栈顶的左括号来配对。", { p: i, hl: hl, done: i, stack: st.slice(), topHl: st.length ? 'cmp' : null });
            if (!st.length) {
              hl[i] = 'warn';
              rec.step(8, '栈是空的，没有左括号可以配对：右括号多了，不匹配。', { p: i, hl: hl, done: i, stack: [], vars: { ok: 0 } });
              ok = false;
            } else {
              var open = st.pop();
              if (open !== PAIR[c]) {
                hl[i] = 'warn';
                rec.step(10, "栈顶是 '" + open + "'，和 '" + c + "' 不是一对：括号交叉了，不匹配。", { p: i, hl: hl, done: i, stack: st.concat([open]), topHl: 'warn', vars: { ok: 0 } });
                ok = false;
              } else {
                hl[i] = 'ok';
                rec.step(7, "栈顶 '" + open + "' 和 '" + c + "' 是一对，弹出。", { p: i, hl: hl, done: i, stack: st.slice() });
              }
            }
          } else {
            rec.step(2, "'" + c + "' 不是括号，跳过。", { p: i, hl: hl, done: i, stack: st.slice() });
          }
        }
        if (!ok) return;
        if (st.length) {
          rec.step(14, '扫描结束，但栈中还剩 ' + st.length + ' 个左括号没有配对：左括号多了，不匹配。', { done: m.text.length, stack: st.slice(), topHl: 'warn', vars: { ok: 0 } });
        } else {
          rec.step(14, '扫描结束，栈为空：所有的括号都正确配对了。', { done: m.text.length, stack: [], vars: { ok: 1 } });
        }
      }
    }]
  });

  // ---------- 中缀转后缀 ----------
  function prec(c) { return c === '*' || c === '/' ? 2 : (c === '+' || c === '-' ? 1 : 0); }
  DSViz.register('postfix', {
    title: 'toPostfix：中缀表达式转后缀表达式',
    height: 270,
    code: [
      'if (isdigit(c)) {',
      '    out[k++] = c;',
      '} else if (c == \'(\') {',
      '    push(&ops, c);',
      '} else if (c == \')\') {',
      '    while (pop(&ops, &top) && top != \'(\')',
      '        out[k++] = top;',
      '} else {',
      '    while (peek(&ops, &top) &&',
      '           precedence(top) >= precedence(c)) {',
      '        pop(&ops, &top);',
      '        out[k++] = top;',
      '    }',
      '    push(&ops, c);',
      '}',
      '/* 扫描结束后，弹出所有剩余的运算符 */'
    ],
    init: function () { return { text: '3+4*(2-1)' }; },
    view: function (m, x) {
      return {
        kind: 'multi', vars: x.vars || {},
        parts: [strPart(m.text.split(''), { hl: x.hl, done: x.done, ptrs: x.p != null ? [{ name: 'c', at: x.p }] : [], top: 70, label: '中缀表达式' }),
          stackPart(x.ops || [], { topHl: x.topHl, x0: 600, top: 40, cap: 5, label: '运算符栈' }),
          strPart(x.out || [], { hl: x.outHl, top: 196, label: '后缀表达式（输出）' })]
      };
    },
    start: function (m, rec) { rec.step(0, '数字直接输出；运算符先放进栈中，等确定了它的右边没有更高优先级的运算符之后再输出。可以输入只含一位数字、+ - * / 和括号的表达式。'); },
    actions: [{
      label: '转换',
      fields: [{ key: 'text', label: '表达式', value: '3+4*(2-1)', size: 14 }],
      check: function (m, p) {
        var t = String(p.text);
        if (!/^[0-9+\-*/()]+$/.test(t)) return '只能包含一位数字、+ - * / 和括号';
        return t.length > 14 ? '最多 14 个字符' : null;
      },
      run: function (m, p, rec) {
        m.text = String(p.text);
        var ops = [], out = [];
        function st(line, msg, i, extra) {
          extra = extra || {};
          var hl = {}; if (i != null) hl[i] = 'cur';
          var oh = {}; if (extra.outNew) oh[out.length - 1] = 'new';
          rec.step(line, msg, { p: i, hl: hl, done: i, ops: ops.slice(), out: out.slice(), outHl: oh, topHl: extra.topHl, vars: { '输出': out.join('') || '（空）' } });
        }
        for (var i = 0; i < m.text.length; i++) {
          var c = m.text[i];
          if (/[0-9]/.test(c)) {
            out.push(c);
            st(2, "'" + c + "' 是数字，直接输出。", i, { outNew: true });
          } else if (c === '(') {
            ops.push(c);
            st(4, "'(' 入栈，它像一道隔墙：在遇到对应的 ')' 之前，它下面的运算符都不会被弹出。", i, { topHl: 'new' });
          } else if (c === ')') {
            st(6, "遇到 ')'，弹出栈中的运算符，直到 '('。", i);
            while (ops.length) {
              var t = ops.pop();
              if (t === '(') { st(6, "弹出 '(' 并丢弃，括号内的部分处理完毕。", i); break; }
              out.push(t);
              st(7, "输出 '" + t + "'。", i, { outNew: true });
            }
          } else {
            st(9, "'" + c + "' 是运算符（优先级 " + prec(c) + "）。比较它和栈顶运算符的优先级。", i, { topHl: ops.length ? 'cmp' : null });
            while (ops.length && prec(ops[ops.length - 1]) >= prec(c)) {
              var o = ops.pop();
              out.push(o);
              st(12, "栈顶的 '" + o + "' 优先级不低于 '" + c + "'，它应该先计算，弹出并输出。", i, { outNew: true });
            }
            ops.push(c);
            st(14, "'" + c + "' 入栈。", i, { topHl: 'new' });
          }
        }
        while (ops.length) {
          var r = ops.pop();
          out.push(r);
          st(16, "扫描结束，弹出剩余的 '" + r + "'。", m.text.length, { outNew: true });
        }
        st(16, '转换完成：' + out.join('') + '。后缀表达式中没有括号，运算的顺序完全由符号的排列决定。', m.text.length);
      }
    }]
  });
})();
