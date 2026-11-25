/* 设计模式专栏《RAII 与 Scope Guard》：栈帧内多个 RAII 对象（Demuxer→Decoder→FrameBuf→ScopeGuard）在正常返回、中途失败、ScopeGuard 回滚下的构造与逆序析构（dp-raii） */
(function () {
  'use strict';

  var LX = 20, RX = 385, BW = 325, BH = 36, GAP = 8;

  function initState(m) {
    m.stack = [];        // 栈帧中存活的 RAII 对象（自底向上压栈，栈顶最先析构）
    m.res = {
      dm: '未分配',
      dec: '未分配',
      fb: '未分配',
      tx: '未开始'
    };
    m.live = 0;
    m.phase = '准备进入 decodeFirstFrame()';
  }

  function view(m, x) {
    x = x || {};
    var hl = x.hl || {}, boxes = [], arrows = [], texts = [];

    // 顶部状态栏
    boxes.push({
      id: 'status', x: 20, y: 14, w: 690, h: 46,
      text: m.phase,
      sub: '当前未释放 C 句柄数 g_live = ' + m.live + (m.live === 0 ? '（零泄漏）' : '（由栈对象托管）'),
      hl: hl.status
    });

    // 左列：当前栈帧（LIFO）
    texts.push({ id: 'ts', x: LX, y: 82, text: '函数栈帧（按构造逆序 LIFO 自动析构）', cls: 'muted' });
    if (!m.stack.length) {
      boxes.push({ id: 'empty_s', x: LX, y: 94, w: BW, h: BH, text: '（栈帧为空）', hl: hl.empty_s });
    } else {
      m.stack.forEach(function (item, idx) {
        // 栈顶画在最上方，直观展示“后构造先析构”
        var row = m.stack.length - 1 - idx;
        boxes.push({
          id: 'st_' + item.id, x: LX, y: 94 + row * (BH + GAP), w: BW, h: BH,
          text: (row === 0 ? '▶ ' : '  ') + item.label,
          sub: item.sub,
          hl: hl['st_' + item.id]
        });
      });
    }

    // 右列：底层 C 句柄与事务状态
    texts.push({ id: 'tr', x: RX, y: 82, text: '底层 C 资源与事务状态', cls: 'muted' });
    var items = [
      { id: 'dm',  name: 'Demuxer 句柄',  state: m.res.dm },
      { id: 'dec', name: 'Decoder 句柄',  state: m.res.dec },
      { id: 'fb',  name: 'FrameBuf 缓冲', state: m.res.fb },
      { id: 'tx',  name: '管线事务 / 临时文件', state: m.res.tx }
    ];
    items.forEach(function (r, i) {
      boxes.push({
        id: 'res_' + r.id, x: RX, y: 94 + i * (BH + GAP), w: BW, h: BH,
        text: r.name + '：' + r.state,
        hl: hl['res_' + r.id]
      });
    });

    if (x.arrow) arrows.push(x.arrow);
    if (x.note) texts.push({ id: 'note', x: 20, y: 344, text: x.note, cls: 'muted' });
    return { kind: 'free', boxes: boxes, arrows: arrows, texts: texts };
  }

  function normalCommit(m, p, rec) {
    initState(m);
    rec.step(1, '进入 decodeFirstFrame()：栈帧为空，g_live = 0。', { hl: { status: 'cur' } });

    m.stack.push({ id: 'dm', label: 'DemuxerPtr dm', sub: 'unique_ptr<Demuxer, DemuxerDeleter>' });
    m.res.dm = '已打开 (#1)'; m.live = 1; m.phase = '构造 dm（资源获取即初始化）';
    rec.step(1, '构造 DemuxerPtr dm：打开解封装器，所有权立刻绑定到栈对象 dm。', {
      hl: { st_dm: 'new', res_dm: 'new' },
      arrow: { from: 'st_dm', to: 'res_dm', hl: 'new', label: 'open_demuxer' }
    });

    m.stack.push({ id: 'dec', label: 'DecoderPtr dec', sub: 'unique_ptr<Decoder, DecoderDeleter>' });
    m.res.dec = '已打开 (#2)'; m.live = 2; m.phase = '构造 dec（压在 dm 之上）';
    rec.step(2, '构造 DecoderPtr dec：打开解码器，压在栈帧中 dm 的上方。', {
      hl: { st_dec: 'new', res_dec: 'new' },
      arrow: { from: 'st_dec', to: 'res_dec', hl: 'new', label: 'open_decoder' }
    });

    m.stack.push({ id: 'fb', label: 'FrameHandle fb', sub: '手写只移动 RAII Handle' });
    m.res.fb = '已分配 (#3)'; m.live = 3; m.phase = '构造 fb（压在 dec 之上）';
    rec.step(3, '构造 FrameHandle fb：分配帧缓冲，3 个句柄均由栈对象托管。', {
      hl: { st_fb: 'new', res_fb: 'new' },
      arrow: { from: 'st_fb', to: 'res_fb', hl: 'new', label: 'alloc_frame' }
    });

    m.stack.push({ id: 'gd', label: 'ScopeGuard rollback [active]', sub: '析构时回滚事务并删临时文件' });
    m.res.tx = '进行中（已建临时文件）'; m.phase = '登记 ScopeGuard 事务回滚守卫';
    rec.step(4, '登记 ScopeGuard rollback（active=true）：若后续任何分支提前返回，析构时自动回滚。', {
      hl: { st_gd: 'new', res_tx: 'cmp' }
    });

    m.stack[3].label = 'ScopeGuard rollback [dismissed]';
    m.stack[3].sub = '已调用 dismiss()，析构时无操作';
    m.res.tx = '已提交（成功）'; m.phase = '解码成功 → 调用 rollback.dismiss() → return 0';
    rec.step(6, '解码与提交成功！调用 rollback.dismiss() 取消回滚，准备正常 return 0。', {
      hl: { status: 'ok', st_gd: 'ok', res_tx: 'ok' }
    });

    m.stack.pop();
    m.phase = '离开作用域：LIFO 析构 ① ~ScopeGuard()（已 dismiss，跳过）';
    rec.step(7, '离开作用域，栈展开 ①：~ScopeGuard() 检查 active==false，不触发回滚。', {
      hl: { res_tx: 'ok' }
    });

    m.stack.pop(); m.res.fb = '已释放'; m.live = 2;
    m.phase = '离开作用域：LIFO 析构 ② ~FrameHandle() → free_frame';
    rec.step(7, '栈展开 ②：~FrameHandle() 释放 FrameBuf，g_live 降至 2。', {
      hl: { res_fb: 'ok' }
    });

    m.stack.pop(); m.res.dec = '已关闭'; m.live = 1;
    m.phase = '离开作用域：LIFO 析构 ③ ~DecoderPtr() → close_decoder';
    rec.step(7, '栈展开 ③：~DecoderPtr() 关闭 Decoder，g_live 降至 1。', {
      hl: { res_dec: 'ok' }
    });

    m.stack.pop(); m.res.dm = '已关闭'; m.live = 0;
    m.phase = '离开作用域：LIFO 析构 ④ ~DemuxerPtr() → close_demuxer（完成）';
    rec.step(7, '栈展开 ④：~DemuxerPtr() 关闭 Demuxer，函数返回 0，g_live = 0，严格逆序且零泄漏！', {
      hl: { status: 'ok', empty_s: 'ok', res_dm: 'ok' },
      note: '构造顺序：dm → dec → fb → guard；析构顺序：guard → fb → dec → dm'
    });
  }

  function earlyFail(m, p, rec) {
    initState(m);
    m.stack.push({ id: 'dm', label: 'DemuxerPtr dm', sub: 'unique_ptr<Demuxer, DemuxerDeleter>' });
    m.res.dm = '已打开 (#1)'; m.live = 1;
    m.stack.push({ id: 'dec', label: 'DecoderPtr dec', sub: 'unique_ptr<Decoder, DecoderDeleter>' });
    m.res.dec = '已打开 (#2)'; m.live = 2;
    m.phase = 'dm、dec 已构造，准备分配 FrameBuf';
    rec.step(2, '已成功打开 Demuxer 与 Decoder（g_live = 2），下一步调用 alloc_frame()。', {
      hl: { st_dm: 'cur', st_dec: 'cur', res_dm: 'cur', res_dec: 'cur' }
    });

    m.res.fb = '分配失败 (nullptr)';
    m.phase = 'alloc_frame() 失败 → 直接提前 return -3';
    rec.step(3, 'alloc_frame() 返回 nullptr，代码直接写 return -3，无需手写一行清理代码！', {
      hl: { status: 'warn', res_fb: 'warn' }
    });

    m.stack.pop(); m.res.dec = '已关闭'; m.live = 1;
    m.phase = '编译器自动展开栈：① ~DecoderPtr() → close_decoder';
    rec.step(7, '编译器只析构「已构造完成」的对象：① 先析构栈顶 dec，g_live 降至 1。', {
      hl: { res_dec: 'ok' }
    });

    m.stack.pop(); m.res.dm = '已关闭'; m.live = 0;
    m.phase = '编译器自动展开栈：② ~DemuxerPtr() → close_demuxer（零泄漏）';
    rec.step(7, '② 再析构栈底 dm，返回 -3，g_live = 0。不管新增多少条 return 路径，绝不可能漏释放。', {
      hl: { status: 'ok', empty_s: 'ok', res_dm: 'ok' },
      note: '只析构已构造的栈对象：fb 未构造成功，因此不会误调 free_frame'
    });
  }

  function guardRollback(m, p, rec) {
    initState(m);
    m.stack.push({ id: 'dm', label: 'DemuxerPtr dm', sub: 'unique_ptr<Demuxer, DemuxerDeleter>' });
    m.stack.push({ id: 'dec', label: 'DecoderPtr dec', sub: 'unique_ptr<Decoder, DecoderDeleter>' });
    m.stack.push({ id: 'fb', label: 'FrameHandle fb', sub: '手写只移动 RAII Handle' });
    m.stack.push({ id: 'gd', label: 'ScopeGuard rollback [active]', sub: '析构时回滚事务并删临时文件' });
    m.res.dm = '已打开 (#1)'; m.res.dec = '已打开 (#2)'; m.res.fb = '已分配 (#3)';
    m.res.tx = '进行中（已建临时文件）'; m.live = 3;
    m.phase = '4 个栈对象全部就绪，开始解码校验';
    rec.step(4, 'dm、dec、fb 与 ScopeGuard rollback 均已压栈（g_live = 3），开始解码校验。', {
      hl: { st_gd: 'cur', res_tx: 'cmp' }
    });

    m.phase = '解码校验失败！在 dismiss() 之前提前 return -4';
    rec.step(5, '解码校验失败，直接 return -4——此时尚未调用 rollback.dismiss()，active 仍为 true。', {
      hl: { status: 'warn', st_gd: 'warn' }
    });

    m.stack.pop(); m.res.tx = '已回滚（临时文件已删）';
    m.phase = '栈展开 ①：~ScopeGuard() 自动执行回滚 lambda！';
    rec.step(7, '栈展开 ①：栈顶 rollback 析构，发现 active==true，自动回滚配置并删除临时文件。', {
      hl: { res_tx: 'ok' },
      arrow: { from: 'status', to: 'res_tx', hl: 'ok', label: '自动回滚' }
    });

    m.stack.pop(); m.res.fb = '已释放'; m.live = 2;
    m.stack.pop(); m.res.dec = '已关闭'; m.live = 1;
    m.stack.pop(); m.res.dm = '已关闭'; m.live = 0;
    m.phase = '栈展开 ②~④：fb → dec → dm 依次逆序释放（完成）';
    rec.step(7, '接着 fb → dec → dm 依次逆序析构，返回 -4，事务回滚干净且 g_live = 0！', {
      hl: { status: 'ok', empty_s: 'ok', res_fb: 'ok', res_dec: 'ok', res_dm: 'ok' },
      note: 'ScopeGuard 把任意一次性回滚逻辑也纳入了栈展开的确定性保证'
    });
  }

  DSViz.register('dp-raii', {
    title: 'RAII 与 Scope Guard：栈帧构造与 LIFO 逆序析构',
    width: 730,
    height: 360,
    code: [
      'DemuxerPtr dm(open_demuxer(url));              // 1. 构造即获取',
      'DecoderPtr dec(open_decoder(dm.get()));        // 2. 压栈',
      'FrameHandle fb(alloc_frame(size)); if (!fb) return -3;',
      'auto rollback = makeScopeGuard([&]{ undo(); });// 4. 登记回滚守卫',
      'if (!decodeOk) return -4;                      // 失败：未 dismiss，触发回滚',
      'rollback.dismiss(); return 0;                  // 成功：取消回滚，正常提交',
      '// 离开作用域：编译器按 LIFO 逆序自动调用各对象析构函数'
    ],
    init: function () { var m = {}; initState(m); return m; },
    view: view,
    start: function (m, rec) { normalCommit(m, {}, rec); },
    actions: [
      { label: '正常返回 + dismiss() 提交', run: normalCommit },
      { label: '中途失败提前 return', run: earlyFail },
      { label: '解码失败触发 ScopeGuard 回滚', run: guardRollback }
    ]
  });
})();
