/* 《队列》：循环队列的入队、出队和下标回绕 */
(function () {
  'use strict';
  var CAP = 6;

  function view(m, x) {
    var cells = [];
    for (var i = 0; i < CAP; i++) {
      var used = m.front <= m.rear ? (i >= m.front && i < m.rear) : (i >= m.front || i < m.rear);
      cells.push(used ? { v: m.data[i], hl: (x.hl || {})[i] } : { v: x.stale && m.data[i] != null ? m.data[i] : '', hl: (x.hl || {})[i] || 'dim' });
    }
    var size = (m.rear + CAP - m.front) % CAP;
    return {
      kind: 'array', cells: cells, cellW: 60, x0: 120, top: 70, ptrs: [{ name: 'front', at: m.front }, { name: 'rear', at: m.rear, hl: 'warn' }],
      label: '数组长度 ' + CAP + '，最多存放 ' + (CAP - 1) + ' 个元素（灰色的格子不属于队列）',
      vars: x.vars || { front: m.front, rear: m.rear, size: size }
    };
  }
  function one(i, c) { var o = {}; o[i] = c; return o; }

  DSViz.register('circular-queue', {
    title: '循环队列：front 和 rear 走到末尾后回到 0',
    height: 150,
    code: [
      'int enqueue(Queue* q, int value)',
      '{',
      '    if ((q->rear + 1) % q->capacity == q->front) return 0;',
      '    q->data[q->rear] = value;',
      '    q->rear = (q->rear + 1) % q->capacity;',
      '    return 1;',
      '}',
      '',
      'int dequeue(Queue* q, int* out)',
      '{',
      '    if (q->front == q->rear) return 0;',
      '    *out = q->data[q->front];',
      '    q->front = (q->front + 1) % q->capacity;',
      '    return 1;',
      '}'
    ],
    init: function () { return { data: [10, 20, 30], front: 0, rear: 3, next: 40 }; },
    view: view,
    start: function (m, rec) { rec.step(0, 'front 指向队头元素，rear 指向下一个元素要放的位置。多做几次入队和出队，观察 rear 和 front 到达末尾后怎样回到 0。'); },
    actions: [{
      label: '入队',
      fields: [{ key: 'value', label: 'value', value: 40, size: 4 }],
      check: function (m, p) { return Number.isInteger(p.value) ? null : 'value 要是整数'; },
      run: function (m, p, rec) {
        var nr = (m.rear + 1) % CAP;
        rec.step(3, '判断队满：(rear + 1) % ' + CAP + ' = ' + nr + (nr === m.front ? '，等于 front，队列已满，入队失败。为了和「队空」区分，数组中总要留一个空位。' : '，不等于 front，还有空间。'), { hl: one(m.rear, nr === m.front ? 'warn' : 'new') });
        if (nr === m.front) return;
        m.data[m.rear] = p.value;
        rec.step(4, '把 ' + p.value + ' 放在 data[' + m.rear + ']。', { hl: one(m.rear, 'new') });
        var old = m.rear;
        m.rear = nr;
        rec.step(5, 'rear = (' + old + ' + 1) % ' + CAP + ' = ' + m.rear + (m.rear === 0 ? '：到达数组末尾后回到了 0，这就是「循环」。' : '。'), { hl: one(old, 'ok') });
      }
    }, {
      label: '出队',
      run: function (m, p, rec) {
        rec.step(11, '判断队空：front == rear 吗？' + (m.front === m.rear ? '是，队列为空，出队失败。' : '不是。'));
        if (m.front === m.rear) return;
        rec.step(12, '取出队头 data[' + m.front + '] = ' + m.data[m.front] + '。先进入队列的元素先出来。', { hl: one(m.front, 'del') });
        var old = m.front;
        m.front = (m.front + 1) % CAP;
        rec.step(13, 'front = (' + old + ' + 1) % ' + CAP + ' = ' + m.front + '。data[' + old + '] 中的值还在，但已经不属于队列了，之后会被新元素覆盖。不需要移动任何元素。', { stale: true });
      }
    }]
  });
})();
