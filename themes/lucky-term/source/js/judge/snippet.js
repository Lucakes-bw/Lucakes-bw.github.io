/**
 * 文章代码块「跟练」：能在浏览器里运行的 C/C++ 代码块（判定规则见 snippet-core.js）
 *   标题栏的 ▶ run  编译运行，输出显示在代码块下面
 *   代码下方的一条  「改一改」和「动手练 · N 关」的入口；练习时变成操作栏（期望输出、重来、运行、检查答案），
 *                   通过后变成通过条（下一关 →）。练习时代码上方是关卡头：关卡步骤、本关标题和说明。练法：
 *           改一改  原代码随便改着跑
 *           填空    挖掉几行，补上后输出和原程序一致就算通过
 *           照着敲  直接在变灰的原代码上敲，敲过的恢复高亮（见 snippet-type.js）；敲完就算通过
 *           默写    原代码藏起来，自己写出输出一样的程序
 *         填空、默写的判定是「输出和原程序一致」：原程序也在浏览器里跑一遍，输出里的地址不比；
 *         用到时间、随机数的程序输出不固定，只要求能正常运行。
 * 草稿按代码块和练法保存在本机，文章里的代码更新后作废；通过记录存在 lt-practice（首页学习动态用）。
 * 编译器和做题页共用（ide.js / cpp-worker.js / wasi-worker.js），第一次运行时才下载。
 *
 * Notebook（front-matter 写 notebook: true，拼接规则见 snippet-core.js 的 assemble）：
 *   文章里的代码块连成一个程序，像 Jupyter 一样后面的格子能用前面的定义。运行第 N 格 = 把第 1～N 格拼起来编译运行，
 *   输出按格子切开，显示在各自下面；开着编辑器的格子用编辑器里的代码，所以改了前面的函数，后面的测试结果会跟着变。
 *   跟练的判定用整篇程序：把这一格换成你写的，其余用原文，输出和全用原文时一致就算通过。
 */
(function () {
  'use strict';

  const $ = (s, el = document) => el.querySelector(s);
  const $$ = (s, el = document) => Array.from(el.querySelectorAll(s));
  const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  const store = {
    get(k) { try { return localStorage.getItem(k); } catch (e) { return null; } },
    set(k, v) { try { localStorage.setItem(k, v); } catch (e) { /* 隐私模式下忽略 */ } },
    del(k) { try { localStorage.removeItem(k); } catch (e) { /* ignore */ } },
    json(k, d) { try { const v = localStorage.getItem(k); return v === null ? d : JSON.parse(v); } catch (e) { return d; } }
  };

  const S = window.LTSnippet, IDE = window.LTIde, CFG = window.LT_SNIPPET;
  const TIME_LIMIT = 10000;
  const isMac = /Mac|iPhone|iPad/.test(navigator.platform);
  const MOD = isMac ? '⌘' : 'Ctrl';
  const fmtMs = ms => (ms < 1 ? '< 1' : Math.round(ms)) + ' ms';
  const PAGE = location.pathname;

  const MODES = {
    edit: { name: '改一改', tip: `随便改，改完点 ▶ 运行（${MOD}+Enter）看看结果有什么变化` },
    fill: { name: '填空', level: 1, tip: '把高亮的空补上（行尾注释是提示），输出和原程序一致就算通过' },
    type: { name: '照着敲', level: 2, tip: '点一下代码，在灰色的原文上直接敲：缩进、注释、中文自动跳过，敲错了要改对才能继续' },
    recall: { name: '默写', level: 3, tip: '不看原文，写出输出一样的程序；实在想不起来可以偷看一眼' }
  };
  const LEVEL_NAME = ['', '填空', '照着敲', '默写'];

  /* ---------- 练习记录：{ 页面路径: { 代码哈希: { fill: 时间, type: 时间, recall: 时间 } } } ---------- */
  const REC_KEY = 'lt-practice';
  const records = () => store.json(REC_KEY, {});
  function record(hash, mode) {
    const all = records();
    const page = all[PAGE] = all[PAGE] || {};
    const r = page[hash] = page[hash] || {};
    r[mode] = Date.now();
    store.set(REC_KEY, JSON.stringify(all));
  }
  const recordOf = hash => (records()[PAGE] || {})[hash] || {};
  const levelOf = r => Math.max(0, ...Object.keys(r).map(m => (MODES[m] && MODES[m].level) || 0));

  /* ---------- 编译器（整页共用一个） ---------- */
  let active = null;                       // 正在显示进度的代码块
  const cc = IDE.compiler(CFG.cppWorker, CFG.clang, {
    progress: (done, total) => { if (active) active.status(`下载编译器 ${Math.min(99, Math.floor(done / total * 100))}%（约 23MB，之后会缓存）`, 'busy'); }
  });

  /* ---------- 编辑器（第一次点 edit 时加载 Monaco） ---------- */
  let monacoP = null;
  function monaco() {
    monacoP = monacoP || IDE.loadMonaco().then(async m => {
      await (document.fonts ? document.fonts.ready : Promise.resolve());
      IDE.applyTheme(m);
      if (window.LTCppComplete) window.LTCppComplete.register(m);
      return m;
    });
    return monacoP;
  }
  // 代码块前一行的 <!-- run: ... --> 注释
  function optionComment(fig) {
    for (let n = fig.previousSibling; n; n = n.previousSibling) {
      if (n.nodeType === 8) return n.data;
      if (n.nodeType === 3 && !n.textContent.trim()) continue;
      return '';
    }
    return '';
  }

  const snippets = [];                     // 本页所有可运行的代码块，文末汇总用
  let seq = 0;

  function setup(fig) {
    const lang = S.normLang((fig.className.replace('highlight', '').trim() || '').split(/\s+/)[0]);
    if (!lang) return;
    const lines = $$('.code .line', fig).map(l => l.textContent);
    const original = lines.length ? lines.join('\n') : ($('code', fig) || fig).textContent;
    const opt = S.parseOptions(optionComment(fig));
    const cell = NB && NB.byFig.get(fig);    // notebook 里的一格（def / main）；自带 main 的、跳过的不算
    const info = cell ? S.classify(lang, original, Object.assign({}, opt, { mode: 'on' })) : S.classify(lang, original, opt);
    const head = $('.code-head', fig);
    if (!info.runnable || !head) return;

    const id = ++seq;
    const hash = S.hash(original);
    const draftKey = mode => `lt-snip:${PAGE}:${hash}${mode === 'edit' ? '' : ':' + mode}`;
    const scroll = $('.code-scroll', fig) || $('table', fig);
    const practicable = !info.error;       // 演示错误的代码只能改一改
    const unstable = cell ? NB.cells.some(c => S.unstable(c.original)) : S.unstable(original);
    const blankLines = practicable ? S.blanks(original, opt.blank) : [];
    fig.classList.add('is-runnable');
    const canFill = blankLines.length > 0;  // 太短的程序（比如 hello world）挖不出空，不提供填空
    const snip = { fig, hash, practicable, canFill };
    snippets.push(snip);

    const acts = document.createElement('span');
    acts.className = 'code-acts';
    acts.innerHTML = `<span class="code-badge" data-badge hidden></span>`
      + `<button type="button" class="code-btn" data-snip-run title="编译运行（${MOD}+Enter）">▶ run</button>`;
    head.insertBefore(acts, $('.code-copy', head));
    // 代码下方的一条：阅读时是「改一改 / 动手练 N 关」的入口，练习时是操作栏，通过后变成通过条
    const strip = document.createElement('div');
    strip.className = 'snip-strip';
    fig.appendChild(strip);
    let idxEl = null;
    if (cell) {
      idxEl = document.createElement('span');
      idxEl.className = 'nb-idx';
      idxEl.textContent = `[${cell.num}]`;
      idxEl.title = cell.kind === 'main' ? '语句格：放进 main 里运行，能用前面各格的定义' : '定义格：后面的格子能用这里的函数和类型';
      head.insertBefore(idxEl, head.firstChild);
    }
    const runBtn = $('[data-snip-run]', acts), badge = $('[data-badge]', acts);

    let mode = null;                       // 打开编辑器时的练法
    let editor = null, m = null, edBox = null, bar = null, expectEl = null, fallback = null;
    const models = {};
    let busy = false, panel = null, decoIds = [];
    const fmtTime = t => `${Math.floor(t / 60000)}:${String(Math.floor(t / 1000) % 60).padStart(2, '0')}`;
    const typeStats = st => {
      const min = st.ms / 60000;
      const parts = [`进度 ${st.total ? Math.floor(st.pos / st.total * 100) : 0}%（${st.pos} / ${st.total} 个字符）`];
      if (st.keys) parts.push(`正确率 ${Math.round((st.keys - st.errors) / st.keys * 100)}%`);
      if (min > 0.05) parts.push(`每分钟 ${Math.round(st.pos / min)} 个字符`);
      return parts.join(' · ');
    };
    // 照着敲：直接在原代码块上敲，见 snippet-type.js
    const typer = window.LTTyper.create({
      fig,
      pre: $('td.code pre', fig) || $('pre', fig),
      load: () => store.json(draftKey('type'), null),
      save: st => (st && st.pos ? store.set(draftKey('type'), JSON.stringify(st)) : store.del(draftKey('type'))),
      shortcut: e => { if (e.key === 'Enter' && (e.metaKey || e.ctrlKey)) { e.preventDefault(); run(); } },
      onProgress: (st, hint) => {
        if (st.done) tip(`敲完了 ✓ ${typeStats(st)}`, 'ok');
        else if (hint) tip(`${hint} · ${typeStats(st)}`, 'bad');
        else tip(st.pos || st.keys ? typeStats(st) : MODES.type.tip);
      },
      onDone: async st => {
        tip(`敲完了 ✓ ${typeStats(st)}`, 'ok');
        await run();
        if (!panel || mode !== 'type') return;
        const acc = `正确率 ${st.keys ? Math.round((st.keys - st.errors) / st.keys * 100) : 100}%`;
        const speed = st.ms >= 3000 ? `用时 ${fmtTime(st.ms)} · ${acc} · 每分钟 ${Math.round(st.pos / (st.ms / 60000))} 个字符` : acc;
        showVerdict(true, `${speed}。下面是这段程序的运行结果`);
      }
    });

    // 闯关的关卡：填空（挖不出空的短程序没有）→ 照着敲 → 默写；演示错误的代码没有关卡
    const LEVELS = practicable ? ['fill', 'type', 'recall'].filter(k => k !== 'fill' || canFill) : [];
    const nextLevel = () => LEVELS.find(k => !recordOf(hash)[k]);
    let passedMode = null;                 // 刚通过的那一关：操作栏显示通过条

    function showBadge() {
      const lv = levelOf(recordOf(hash));
      badge.hidden = !lv;
      badge.textContent = lv ? `✓ ${LEVEL_NAME[lv]}` : '';
      badge.title = lv ? `已经通过「${LEVEL_NAME[lv]}」` : '';
      if (bar) $$('[data-mode]', bar).forEach(b => b.classList.toggle('is-passed', !!recordOf(hash)[b.dataset.mode]));
    }
    const hasDraft = () => Object.keys(MODES).some(k => store.get(draftKey(k)) !== null);
    const dots = () => LEVELS.map(k => `<span class="snip-dot${recordOf(hash)[k] ? ' is-on' : ''}" title="${MODES[k].name}${recordOf(hash)[k] ? ' ✓' : ''}"></span>`).join('');

    // 代码下方那一条：阅读 / 练习 / 刚通过 三种样子
    function renderStrip() {
      syncStdin();                         // 切换练法、退出后要运行的代码变了
      strip.classList.toggle('is-pass', !!passedMode);
      if (!mode) {
        const nx = nextLevel(), done = LEVELS.length && !nx;
        const draft = Object.keys(MODES).some(k => k !== 'type' && store.get(draftKey(k)) !== null);
        strip.innerHTML = `<button type="button" class="code-btn${draft ? ' has-draft' : ''}" data-act="edit" title="随便改这段代码，改完运行看看">✎ 改一改</button>`
          + (LEVELS.length ? `<span class="snip-strip-info">动手练 · ${LEVELS.length} 关 <span class="snip-dots">${dots()}</span></span>`
            + `<button type="button" class="btn btn-sm ${done ? 'btn-ghost' : 'btn-solid'}" data-act="go">${done ? '全部通过 ✓ 再练' : recordOf(hash)[LEVELS[0]] ? `继续第 ${LEVELS.indexOf(nx) + 1} 关` : '开始第 1 关'}</button>` : '');
        return;
      }
      if (passedMode) {
        const k = LEVELS.indexOf(passedMode), nx = LEVELS[k + 1];
        strip.innerHTML = `<span class="snip-pass-text"><b>✓ ${k >= 0 ? `第 ${k + 1} 关通过` : '通过'}</b><span data-pass-msg></span></span>`
          + `<button type="button" class="code-btn" data-act="stay">留在这关</button>`
          + (nx ? `<button type="button" class="btn btn-solid btn-sm" data-act="next" data-to="${nx}">下一关：${MODES[nx].name} →</button>`
            : `<button type="button" class="btn btn-solid btn-sm" data-act="exit">全部通过，收起</button>`);
        return;
      }
      const judge = mode === 'fill' || mode === 'recall';
      strip.innerHTML = `<span class="snip-strip-tools">`
        + (judge ? `<button type="button" class="code-btn" data-expect>期望输出</button>` : '')
        + (mode === 'recall' ? `<button type="button" class="code-btn" data-peek>偷看原文</button>` : '')
        + `<button type="button" class="code-btn" data-reset title="恢复成这一关的初始代码">↺ 重来</button></span>`
        + `<button type="button" class="code-btn${judge ? '' : ' is-primary-run'}" data-act="run" title="${MOD}+Enter">▶ 运行</button>`
        + (judge ? `<button type="button" class="btn btn-solid btn-sm snip-check" data-check title="${MOD}+Shift+Enter">检查答案</button>` : '');
    }
    strip.addEventListener('click', e => {
      const t = e.target.closest('button');
      if (!t) return;
      const a = t.dataset.act;
      if (a === 'edit') open('edit');
      else if (a === 'go') open(nextLevel() || LEVELS[0]);
      else if (a === 'run') run();
      else if (a === 'stay') { passedMode = null; renderStrip(); }
      else if (a === 'next') { passedMode = null; switchMode(t.dataset.to); }
      else if (a === 'exit') close();
      else if ('check' in t.dataset) check();
      else if ('reset' in t.dataset) reset();
      else if ('peek' in t.dataset) {
        const on = scroll.hidden;
        scroll.hidden = !on;
        t.classList.toggle('is-on', on);
        t.textContent = on ? '藏起原文' : '偷看原文';
      } else if ('expect' in t.dataset) toggleExpect(t);
    });
    const markDraft = () => { if (!mode) renderStrip(); };
    showBadge();
    renderStrip();

    // 每种练法的初始代码
    const initial = k => (k === 'fill' ? S.blankCode(original, blankLines) : k === 'type' || k === 'recall' ? '' : original);
    const code = () => (!mode || mode === 'type' ? original : editor ? editor.getValue() : fallback ? fallback.value : original);

    // 实际编译的程序。notebook 里是拼起来的第 1～upto 格：这一格用 src，其余格子用各自当前的代码（pristine 时全用原文）
    const LAST = NB ? NB.cells.length - 1 : 0;
    function program(src, upto, pristine) {
      if (!cell) return src;
      return S.assemble(NB.lang, NB.cells.map(c => ({
        kind: c.kind, name: c.name, code: c === cell ? src : pristine ? c.original : c.current()
      })), upto).source;
    }
    // 这一格（含）往后有没有语句格：定义格后面没有测试代码时，跟练只能看编译过没过
    const untested = cell && !NB.cells.some(c => c.index >= cell.index && c.kind === 'main');
    if (cell) {
      cell.current = code;
      cell.show = (text, status, cls) => {
        ensurePanel();
        api.status(status, cls);
        const o = $('[data-out]', panel);
        o.classList.toggle('is-empty', !text);
        o.textContent = text || '（没有输出）';
      };
      cell.busy = on => { if (idxEl) idxEl.textContent = on ? '[*]' : `[${cell.num}]`; };
      cell.runAll = () => run({ upto: LAST, all: true });
    }

    /* --- 编译和运行（同样的代码只编译一次） --- */
    const builds = new Map();
    function build(src) {
      if (!builds.has(src)) {
        if (builds.size > 6) builds.delete(builds.keys().next().value);
        builds.set(src, cc.compile(S.compileArgs(lang, info.flags), S.compileFiles(lang, src)).then(c => {
          if (!c.ok) builds.delete(src);
          return c;
        }));
      }
      return builds.get(src);
    }
    const compilePhase = () => (cc.isReady() ? '编译中…' : '首次运行：下载编译器（约 23MB，之后会缓存）…');

    // 原程序的输出：判定和「期望输出」用，按 stdin 缓存
    const expected = new Map();
    function expectedOutput(stdin) {
      if (!expected.has(stdin)) {
        expected.set(stdin, build(program(original, LAST, true)).then(c => {
          if (!c.ok) return { error: '原程序编译失败' };
          return IDE.runWasm(CFG.wasiWorker, c.wasm, stdin, TIME_LIMIT);
        }).then(r => { if (r.error || r.timeout) expected.delete(stdin); return r; }));
      }
      return expected.get(stdin);
    }
    const stdinValue = () => (panel ? $('textarea', panel).value : info.stdin);

    /* --- 输出面板 --- */
    function ensurePanel() {
      if (panel) return panel;
      panel = document.createElement('div');
      panel.className = 'snip-panel';
      panel.innerHTML = `
        <div class="snip-bar">
          <span class="snip-status" data-st></span>
          <span class="snip-tools">
            <button type="button" class="code-btn" data-stdin-toggle aria-expanded="false">stdin</button>
            <button type="button" class="code-btn" data-close title="关闭输出">×</button>
          </span>
        </div>
        <label class="snip-stdin" hidden><span class="c-muted">标准输入：程序用 scanf / getchar / cin 读到的内容，要在运行前填好（改完再点 ▶ 运行）</span><textarea rows="2" spellcheck="false" autocomplete="off" id="snip-stdin-${id}"></textarea></label>
        <div class="snip-verdict" data-verdict hidden></div>
        <pre class="snip-out" data-out></pre>`;
      fig.insertBefore(panel, strip);
      const ta = $('textarea', panel);
      ta.value = info.stdin;
      const toggle = $('[data-stdin-toggle]', panel);
      toggle.addEventListener('click', () => { const on = $('.snip-stdin', panel).hidden; showStdin(on); if (on) ta.focus(); });
      ta.addEventListener('keydown', e => { if ((e.metaKey || e.ctrlKey) && e.key === 'Enter') { e.preventDefault(); run(); } });
      $('[data-close]', panel).addEventListener('click', () => { panel.remove(); panel = null; });
      syncStdin();
      if (readsNow()) showStdin(true);
      return panel;
    }
    function showStdin(on) {
      if (!panel) return;
      const toggle = $('[data-stdin-toggle]', panel);
      $('.snip-stdin', panel).hidden = !on;
      toggle.setAttribute('aria-expanded', String(on));
      toggle.classList.toggle('is-on', on);
    }
    // 现在要运行的代码读不读输入（改一改里可能刚加上或删掉了 scanf）。
    // 不读输入的程序不显示 stdin：填了也没用，对新手只会添乱
    const readsNow = () => S.readsStdin(cell ? program(code(), cell.index) : code());
    function syncStdin() {
      if (!panel) return;
      const r = readsNow();
      $('[data-stdin-toggle]', panel).hidden = !r;
      if (!r) showStdin(false);
    }
    const api = {
      status(text, cls) {
        const st = $('[data-st]', panel);
        st.className = 'snip-status' + (cls ? ' is-' + cls : '');
        st.innerHTML = (cls === 'busy' ? '<span class="snip-spin" aria-hidden="true"></span>' : '') + text;
      }
    };
    // 要读输入、输入框又是空的：第一次运行前先打开输入框，提示填好再运行（打开过以后空着也照样运行）
    function needStdin() {
      if (!readsNow()) { syncStdin(); return false; }
      const fresh = !panel;
      ensurePanel();
      syncStdin();
      if (stdinValue().trim() || (!fresh && !$('.snip-stdin', panel).hidden)) return false;
      showStdin(true);
      api.status(`这段程序会读取输入（scanf 等）：先在下面填好，再点 ▶ 运行（${MOD}+Enter）`);
      $('textarea', panel).focus();
      return true;
    }

    function mark(diags) {
      if (!editor || !window.monaco) return;
      const mm = window.monaco, model = editor.getModel();
      mm.editor.setModelMarkers(model, 'snippet', diags.filter(d => d.line > 0 && d.line <= model.getLineCount()).map(d => {
        const word = model.getWordAtPosition({ lineNumber: d.line, column: Math.max(1, d.col) });
        return {
          startLineNumber: d.line, startColumn: word ? word.startColumn : Math.max(1, d.col),
          endLineNumber: d.line, endColumn: word ? word.endColumn : model.getLineMaxColumn(d.line),
          message: d.msg, severity: d.severity === 'warning' ? mm.MarkerSeverity.Warning : mm.MarkerSeverity.Error
        };
      }));
    }

    // 编译运行 src，输出写进面板。返回 { ok: 编译通过, run: 运行结果 }
    // notebook：诊断里的文件名换成格子编号
    const cellLabel = n => (/^cell(\d+)$/.test(n) ? `[${n.slice(4)}]` : /^hidden/.test(n) ? '[辅助代码]' : `[${n}]`);
    const clang = text => {
      const t = IDE.cleanClang(text);
      return cell ? t.replace(/\b(cell\d+|hidden\d+|main|prelude)\.(?:c|cpp):/g, (_, n) => cellLabel(n) + ':') : t;
    };
    // nbo: { upto, split（按格子分发输出）, all（每个语句格都显示）}，只在 notebook 里用
    async function execute(src, stdin, nbo) {
      const o = $('[data-out]', panel);
      o.innerHTML = '';
      o.classList.remove('is-empty');
      mark([]);
      api.status(compilePhase(), 'busy');
      const c = await build(src);
      let diags;
      if (cell) {
        const all = S.nbDiagnostics(c.stderr);
        diags = all.filter(d => d.cell === cell.name);
        const other = all.find(d => d.severity === 'error' && d.cell !== cell.name);
        if (!c.ok && other && !diags.some(d => d.severity === 'error')) c.where = cellLabel(other.cell);
      } else diags = S.diagnostics(lang, c.stderr);
      mark(diags);
      // 作者标了 run: error 的代码本来就是演示错误的，没改过时出错给一句说明
      const demo = info.error && src === original ? ' <span class="c-muted">· 这段代码本来就是用来演示这个错误的</span>' : '';
      if (!c.ok) {
        api.status(c.loadError ? '✗ 编译器加载失败' : '✗ 编译出错' + (c.where ? `（错在第 ${c.where} 格，这一格本身没问题）` : '') + demo, 'bad');
        o.innerHTML = `<span class="snip-err">${esc(clang(c.stderr) || '编译失败')}</span>`;
        return { ok: false };
      }
      const warn = clang(c.stderr);
      if (warn) o.innerHTML = `<details class="snip-warn"><summary>编译警告 ${diags.filter(d => d.severity === 'warning').length || ''} 条</summary>${esc(warn)}</details>`;
      api.status('运行中…', 'busy');
      const outNode = document.createTextNode('');
      o.appendChild(outNode);
      const t0 = performance.now();
      let raw = '';
      const r = await IDE.runWasm(CFG.wasiWorker, c.wasm, stdin, TIME_LIMIT, text => {
        if (!cell) { outNode.data += text; return; }
        raw += text;
        outNode.data = S.stripMarks(raw);
      });
      const ms = performance.now() - t0;
      let where = '';
      if (cell && nbo && nbo.split) {
        // 按格子切开：这一格显示自己的那段，其他语句格（已经打开输出的，或者「全部运行」时每一格）显示各自的
        const seg = S.splitOutput(r.stdout);
        const stopped = (r.timeout || r.error) && seg.last != null && seg.last >= 0 ? NB.cells[seg.last] : null;
        if (stopped && stopped !== cell) where = `（停在第 [${stopped.num}] 格）`;
        NB.cells.forEach(x => {
          if (x === cell || x.kind !== 'main' || x.index > nbo.upto || !x.show) return;
          if (!nbo.all && !x.hasPanel()) return;
          const ran = seg.last === -1 || (seg.last != null && seg.last >= x.index);
          x.show(seg.out[x.index] || '', ran ? (x === stopped ? '✗ 运行到这一格出错了' : `✓ 随第 [${cell.num}] 格一起运行`) : '没有运行到这一格', x === stopped ? 'bad' : ran ? 'ok' : '');
        });
        outNode.data = cell.kind === 'main' ? (seg.out[cell.index] || '') : '';
      } else outNode.data = cell ? S.stripMarks(r.stdout) : r.stdout;
      if (r.stderr) o.insertAdjacentHTML('beforeend', `<span class="snip-err">${esc(r.stderr)}</span>`);
      if (r.timeout) {
        api.status(`✗ 超时：运行超过 ${TIME_LIMIT / 1000} 秒，已停止${where}${demo || '（死循环，或者在等输入？）'}`, 'bad');
      } else if (r.error) {
        api.status('✗ 运行出错' + where + demo, 'bad');
        o.insertAdjacentHTML('beforeend', `<span class="snip-err">${esc(r.error)}</span>`);
      } else {
        const nbNote = cell && nbo && nbo.split ? (cell.kind === 'main' ? (cell.num > 1 ? ` · 连同前面 ${cell.num - 1} 格${NB.cells.some(c => c.kind === 'hidden' && c.index < cell.index) ? '和辅助代码' : ''}一起编译` : '') : ' · 定义格本身没有输出，编译通过') : '';
        api.status(`✓ 运行结束 · ${fmtMs(ms)}${nbNote}${r.exitCode ? ` · <span class="c-warn">退出码 ${r.exitCode}</span>${demo}` : ''}`, 'ok');
      }
      if (!outNode.data && !r.stderr && !r.error && !warn) { o.classList.add('is-empty'); o.textContent = cell && cell.kind !== 'main' && nbo && nbo.split ? '（定义格没有输出：后面的语句格会用到它）' : '（没有输出）'; }
      return { ok: true, run: r };
    }

    async function guarded(fn) {
      if (busy) return;
      busy = true;
      runBtn.disabled = true;
      $$('[data-check], [data-act="run"]', strip).forEach(b => { b.disabled = true; });
      active = api;
      if (cell) cell.busy(true);
      try {
        await fn();
      } catch (e) {
        api.status('✗ 出错了', 'bad');
        $('[data-out]', panel).textContent = String(e && e.stack || e);
      } finally {
        if (cell) cell.busy(false);
        busy = false;
        runBtn.disabled = false;
        $$('[data-check], [data-act="run"]', strip).forEach(b => { b.disabled = false; });
      }
    }

    function run(nbo) {
      if (needStdin()) return;
      ensurePanel();
      $('[data-verdict]', panel).hidden = true;
      if (!cell) return guarded(() => execute(code(), stdinValue()));
      const o = Object.assign({ upto: cell.index, split: true }, nbo && nbo.upto != null ? nbo : null);
      return guarded(() => execute(program(code(), o.upto), stdinValue(), o));
    }

    /* --- 判定 --- */
    async function check() {
      if (!mode || mode === 'edit') return;
      const src = code();
      if (!src.trim()) { tip('还什么都没写'); return; }
      if (mode === 'fill') {
        const left = src.split('\n').filter(l => l.includes(S.BLANK_MARK)).length;
        if (left) { tip(`还有 ${left} 个空没填：把 /* … ${S.BLANK_MARK} */ 换成代码`, 'bad'); return; }
      }
      if (needStdin()) return;
      ensurePanel();
      const verdict = $('[data-verdict]', panel);
      verdict.hidden = true;
      await guarded(async () => {
        const stdin = stdinValue();
        const mine = await execute(program(src, LAST, true), stdin);
        if (!mine.ok) { showVerdict(false, '编译没通过，先把错误改掉'); return; }
        const r = mine.run;
        if (r.timeout || r.error) { showVerdict(false, '程序没能正常运行完'); return; }
        if (untested) { showVerdict(true, '后面没有用到它的测试代码，编译通过就算通过'); return; }
        if (unstable) {
          showVerdict(true, '这段程序每次运行输出都不一样（用到了时间或随机数），能正常运行就算通过');
          return;
        }
        api.status('运行原程序，对比输出…', 'busy');
        const exp = await expectedOutput(stdin);
        if (exp.error || exp.timeout) { showVerdict(false, '原程序没能运行，无法对比：' + (exp.error || '超时')); return; }
        const cmp = S.compareOutput(S.stripMarks(r.stdout), S.stripMarks(exp.stdout));
        api.status(cmp.ok ? '✓ 运行结束' : '✓ 运行结束，但输出和原程序不一样', cmp.ok ? 'ok' : 'bad');
        if (cmp.ok) showVerdict(true, '输出和原程序完全一致');
        else showVerdict(false, `输出从第 ${cmp.line} 行开始不一样${cell ? '（整篇程序的输出）' : ''}`, cmp, S.stripMarks(exp.stdout));
      });
    }
    function showVerdict(ok, text, cmp, expOut) {
      const v = $('[data-verdict]', panel);
      v.hidden = false;
      v.className = 'snip-verdict ' + (ok ? 'is-ok' : 'is-bad');
      let html = `<b>${ok ? `✓ 通过「${MODES[mode].name}」` : '✗ 还没通过'}</b> <span>${esc(text)}</span>`;
      if (cmp) {
        html += `<div class="snip-diff"><div><span class="c-muted">你的第 ${cmp.line} 行</span><code>${esc(cmp.got)}</code></div><div><span class="c-muted">应该是</span><code>${esc(cmp.exp)}</code></div></div>`;
        html += `<details class="snip-expect-all"><summary>原程序的完整输出</summary><pre>${esc(expOut)}</pre></details>`;
      }
      if (ok) {
        // 通过：判定信息放进代码下方的通过条，输出面板里不再重复
        record(hash, mode);
        showBadge();
        renderSummary();
        v.hidden = true;
        passedMode = mode;
        renderStrip();
        $('[data-pass-msg]', strip).textContent = text;
        renderLevelBar();
        return;
      }
      v.innerHTML = html;
    }

    /* --- 编辑器和练法 --- */
    function tip(text, cls) {
      if (!bar) return;
      const t = $('[data-tip]', bar);
      t.textContent = text;
      t.className = 'snip-tip' + (cls ? ' is-' + cls : '') + (mode === 'type' ? ' is-stats' : '');
    }
    function defaultTip() {
      if (mode === 'type') { const st = typer.stats(); tip(st.done ? `已经敲完了 ✓ 想再练一遍点 ↺ 重来` : st.pos ? `接着上次敲 · ${typeStats(st)}` : MODES.type.tip, st.done ? 'ok' : ''); return; }
      let t = MODES[mode].tip;
      if (unstable && mode !== 'edit') t += '。这段程序输出不固定，能正常运行就算通过';
      tip(t);
    }

    // 每一关的标题
    const TITLE = {
      edit: '改一改：随便改',
      fill: () => `填空：补全 ${blankLines.length} 行`,
      type: '照着敲：一字不差地敲一遍',
      recall: '默写：不看原文写出来'
    };
    // 练习时代码上方的关卡头：关卡步骤（可以点着切换）、本关标题和说明、退出
    function buildBar() {
      bar = document.createElement('div');
      bar.className = 'snip-level';
      bar.innerHTML = `
        <div class="snip-level-top">
          <span class="snip-steps" role="tablist" aria-label="关卡" data-steps></span>
          <button type="button" class="code-btn" data-exit title="收起（改动会保留）">退出</button>
        </div>
        <div class="snip-level-title" data-title></div>
        <div class="snip-tip" data-tip></div>`;
      expectEl = document.createElement('div');
      expectEl.className = 'snip-expect';
      expectEl.hidden = true;
      bar.appendChild(expectEl);
      head.after(bar);
      bar.addEventListener('click', e => {
        const t = e.target.closest('[data-mode]');
        if (t) { passedMode = null; switchMode(t.dataset.mode); return; }
        if (e.target.closest('[data-exit]')) close();
      });
    }
    function renderLevelBar() {
      const steps = mode === 'edit'
        ? `<span class="snip-step is-on">改一改</span>`
          + (LEVELS.length ? `<span class="snip-step-sep">·</span><button type="button" class="snip-step" data-mode="${nextLevel() || LEVELS[0]}">去闯关 →</button>` : '')
        : LEVELS.map((k, i) => {
          const done = !!recordOf(hash)[k];
          return (i ? '<span class="snip-step-sep">›</span>' : '')
            + `<button type="button" class="snip-step${k === mode ? ' is-on' : ''}${done ? ' is-passed' : ''}" data-mode="${k}" role="tab" aria-selected="${k === mode}">${done ? '✓' : i + 1} ${MODES[k].name}</button>`;
        }).join('');
      $('[data-steps]', bar).innerHTML = (mode === 'edit' ? '' : `<span class="snip-level-no">第 ${LEVELS.indexOf(mode) + 1} / ${LEVELS.length} 关</span>`) + steps;
      const t = TITLE[mode];
      $('[data-title]', bar).textContent = typeof t === 'function' ? t() : t;
    }
    async function toggleExpect(btn) {
      if (!expectEl.hidden) { expectEl.hidden = true; btn.classList.remove('is-on'); return; }
      btn.classList.add('is-on');
      expectEl.hidden = false;
      if (readsNow() && !stdinValue()) { expectEl.innerHTML = '<span class="c-muted">这段程序要读输入：先在输出面板的 stdin 里填好输入。</span>'; ensurePanel(); $('.snip-stdin', panel).hidden = false; return; }
      expectEl.innerHTML = `<span class="c-muted"><span class="snip-spin" aria-hidden="true"></span> 运行原程序…</span>`;
      active = { status: t => { expectEl.innerHTML = `<span class="c-muted">${t}</span>`; } };
      const r = await expectedOutput(stdinValue());
      if (expectEl.hidden) return;
      expectEl.innerHTML = r.error || r.timeout
        ? `<span class="snip-err">原程序没能运行：${esc(r.error || '超时')}</span>`
        : `<div class="c-muted">${cell ? '整篇程序（全用原文）' : '原程序'}的输出${unstable ? '（每次运行都不一样）' : ''}：</div><pre>${esc(S.stripMarks(r.stdout)) || '（没有输出）'}</pre>`;
    }

    function saveDraft() {
      if (!mode || mode === 'type') return;     // 照着敲的进度由 typer 自己存
      const v = code();
      if (v === initial(mode)) store.del(draftKey(mode)); else store.set(draftKey(mode), v);
    }

    async function open(k) {
      mode = k;
      passedMode = null;
      fig.classList.add('is-editing');
      cc.preload();                          // 开始练的时候就在后台准备编译器
      if (!bar) buildBar();
      bar.hidden = false;
      if (!edBox) {
        edBox = document.createElement('div');
        edBox.className = 'snip-editor';
        edBox.style.height = Math.min(lines.length * 22 + 24, window.innerHeight * 0.7) + 'px';
        edBox.innerHTML = '<div class="snip-loading c-muted">加载编辑器…</div>';
        scroll.after(edBox);
      }
      applyMode();
    }

    async function ensureEditor() {
      if (editor || fallback || ensureEditor.loading) return;
      ensureEditor.loading = true;
      try {
        const mm = await monaco();
        m = mm;
        edBox.innerHTML = '';
        editor = mm.editor.create(edBox, Object.assign({ model: modelFor(mode === 'type' ? 'edit' : mode) }, IDE.editorOptions(), { lineNumbersMinChars: 3 }));
        editor.addCommand(mm.KeyMod.CtrlCmd | mm.KeyCode.Enter, run);
        editor.addCommand(mm.KeyMod.CtrlCmd | mm.KeyMod.Shift | mm.KeyCode.Enter, check);
        editor.onDidContentSizeChange(fit);
        if (mode === 'type') return;
        if (mode === 'fill') highlightBlanks();
        fit();
        defaultTip();
        editor.focus();
      } catch (e) {
        // 编辑器加载失败（比如 CDN 被墙）：退回普通文本框，照样能改能运行
        edBox.innerHTML = '<textarea class="snip-fallback" spellcheck="false"></textarea>';
        edBox.style.height = 'auto';
        fallback = $('textarea', edBox);
        fallback.value = store.get(draftKey(mode)) ?? initial(mode);
        fallback.rows = Math.min(lines.length + 1, 30);
        fallback.addEventListener('input', () => { saveDraft(); syncStdin(); });
        fallback.addEventListener('keydown', e => { if ((e.metaKey || e.ctrlKey) && e.key === 'Enter') { e.preventDefault(); (e.shiftKey ? check : run)(); } });
        if (mode !== 'type') fallback.focus();
      } finally {
        ensureEditor.loading = false;
      }
    }

    function modelFor(k) {
      if (models[k]) return models[k];
      const model = m.editor.createModel(store.get(draftKey(k)) ?? initial(k), lang === 'c' ? 'c' : 'cpp');
      let timer;
      model.onDidChangeContent(() => {
        clearTimeout(timer);
        timer = setTimeout(() => { saveDraft(); syncStdin(); }, 400);
        m.editor.setModelMarkers(model, 'snippet', []);
      });
      return (models[k] = model);
    }

    // 切换练法：换编辑器内容、显示或隐藏原代码
    function applyMode() {
      renderLevelBar();
      renderStrip();
      expectEl.hidden = true;
      const typing = mode === 'type';
      scroll.hidden = !typing;                   // 照着敲直接在原代码块上敲；其他练法用编辑器
      edBox.hidden = typing;
      showBadge();
      if (panel) $('[data-verdict]', panel).hidden = true;
      if (typing) {
        typer.start();
        defaultTip();
        typer.focus();
        return;
      }
      typer.stop();
      if (editor) {
        decoIds = editor.deltaDecorations(decoIds, []);
        editor.setModel(modelFor(mode));
        if (mode === 'fill') highlightBlanks();
        fit();
        defaultTip();
        editor.focus();
      } else if (fallback) {
        fallback.value = store.get(draftKey(mode)) ?? initial(mode);
        defaultTip();
      } else {
        defaultTip();
        ensureEditor();
      }
    }
    function switchMode(k) {
      if (k === mode) return;
      saveDraft();
      mode = k;
      if (panel) { panel.remove(); panel = null; }   // 上一关的输出已经过时了
      applyMode();
    }

    // 填空：把挖掉的行高亮出来
    function highlightBlanks() {
      const model = editor.getModel();
      const ds = [];
      for (let i = 1; i <= model.getLineCount(); i++) {
        if (model.getLineContent(i).includes(S.BLANK_MARK)) ds.push({ range: new m.Range(i, 1, i, 1), options: { isWholeLine: true, className: 'snip-line-blank', linesDecorationsClassName: 'snip-gut-blank' } });
      }
      decoIds = editor.deltaDecorations(decoIds, ds);
    }

    const fit = () => {
      if (!editor) return;
      const h = Math.min(Math.max(editor.getContentHeight(), Math.min(lines.length * 22 + 24, 240), 120), Math.round(window.innerHeight * 0.7));
      edBox.style.height = h + 'px';
    };

    function close() {
      saveDraft();
      mode = null;
      passedMode = null;
      fig.classList.remove('is-editing');
      typer.stop();
      bar.hidden = true;
      edBox.hidden = true;
      scroll.hidden = false;
      if (panel) $('[data-verdict]', panel).hidden = true;
      renderStrip();
    }
    function reset() {
      if (mode === 'type') {
        if (typer.stats().pos && !confirm('照着敲从头开始？')) return;
        typer.reset();
        if (panel) $('[data-verdict]', panel).hidden = true;
        return;
      }
      if (!confirm(`把「${MODES[mode].name}」恢复成初始代码？${editor ? `（之后可以用 ${MOD}+Z 撤销）` : ''}`)) return;
      const text = initial(mode);
      if (editor) {
        const model = editor.getModel();
        editor.pushUndoStop();
        model.pushEditOperations([], [{ range: model.getFullModelRange(), text }], () => null);
        editor.pushUndoStop();
        if (mode === 'fill') highlightBlanks();
        editor.focus();
      } else if (fallback) fallback.value = text;
      store.del(draftKey(mode));
    }

    if (cell) cell.hasPanel = () => !!panel;
    runBtn.addEventListener('click', () => run());
    // 鼠标移到 run 上、或者用键盘选中它，就先在后台下载编译器：点下去时已经下了一截
    const warm = () => cc.preload();
    runBtn.addEventListener('pointerenter', warm, { once: true });
    runBtn.addEventListener('focus', warm, { once: true });
    snip.open = k => { fig.scrollIntoView({ behavior: 'smooth', block: 'start' }); if (!mode) open(k); else switchMode(k); };
  }

  /* ---------- 文末汇总 ---------- */
  let sumEl = null;
  function renderSummary() {
    const list = snippets.filter(s => s.practicable);
    if (!list.length) return;
    if (!sumEl) {
      const anchor = $('.post-end');
      if (!anchor) return;
      sumEl = document.createElement('div');
      sumEl.className = 'practice-sum';
      anchor.insertBefore(sumEl, anchor.firstChild);
      sumEl.addEventListener('click', e => {
        const b = e.target.closest('[data-goto]');
        if (b) list[+b.dataset.goto].open(b.dataset.mode);
      });
    }
    const page = records()[PAGE] || {};
    const levels = list.map(s => levelOf(page[s.hash] || {}));
    const done = levels.filter(Boolean).length;
    const next = levels.findIndex(l => l < 3);
    const nextMode = next < 0 ? null : levels[next] === 0 && !list[next].canFill ? 'type' : ['fill', 'type', 'recall'][levels[next]];
    sumEl.innerHTML = `
      <span class="practice-text"><span class="ui-term"><span class="c-accent">$</span> make practice</span><span class="ui-ios">跟练</span>
        本文有 <b>${list.length}</b> 段代码可以动手练，已通过 <b>${done}</b> 段${done ? `（${[1, 2, 3].map(l => `${LEVEL_NAME[l]} ${levels.filter(x => x >= l).length}`).join(' · ')}）` : ''}</span>
      ${next < 0 ? '<span class="c-accent">全部默写通过 ✓</span>'
        : `<button type="button" class="btn btn-ghost btn-sm" data-goto="${next}" data-mode="${nextMode}">${done ? '继续练' : '开始练'}：第 ${next + 1} 段「${MODES[nextMode].name}」</button>`}`;
  }

  /* ---------- Notebook ---------- */
  const NB = CFG.notebook ? buildNotebook() : null;
  function buildNotebook() {
    const figs = $$('.md figure.highlight').map(fig => {
      const lang = S.normLang((fig.className.replace('highlight', '').trim() || '').split(/\s+/)[0]);
      if (!lang) return null;
      const lines = $$('.code .line', fig).map(l => l.textContent);
      const original = lines.length ? lines.join('\n') : ($('code', fig) || fig).textContent;
      const opt = S.parseOptions(optionComment(fig));
      if (fig.closest('details')) opt.details = true;      // 练习答案里的代码默认不参与拼接
      return { fig, lang, original, kind: S.cellKind(lang, original, opt) };
    }).filter(Boolean);
    const nbLang = S.notebookLang(figs);
    let num = 0, hid = 0;
    const cells = figs.filter(f => /^(def|main|hidden)$/.test(f.kind) && f.lang === nbLang).map((f, index) => Object.assign(f, {
      index,
      num: f.kind === 'hidden' ? null : ++num,
      name: f.kind === 'hidden' ? 'hidden' + (++hid) : 'cell' + num,
      current: () => f.original
    }));
    if (!cells.some(c => c.kind === 'main')) return null;      // 没有语句格，拼起来也跑不出东西
    // 辅助代码默认折叠
    cells.filter(c => c.kind === 'hidden').forEach(c => {
      const d = document.createElement('details');
      d.className = 'nb-hidden';
      d.innerHTML = '<summary>辅助代码：文中没有写出来，运行时会自动带上</summary>';
      c.fig.before(d);
      d.appendChild(c.fig);
    });
    // 第一格前面的说明和「全部运行」
    const first = cells.find(c => c.kind !== 'hidden');
    const bar = document.createElement('div');
    bar.className = 'nb-bar';
    bar.innerHTML = `<span class="nb-text"><span class="ui-term"><span class="c-accent">$</span> notebook</span><span class="ui-ios">Notebook</span>
      这篇文章的 ${num} 段代码是连着的：后面的格子能用前面定义的函数和类型。运行某一格时会连同前面的格子一起运行；改了前面的代码，后面的结果会跟着变。</span>
      <button type="button" class="btn btn-ghost btn-sm" data-nb-all>▶ 全部运行</button>`;
    (first.fig.closest('details.nb-hidden') || first.fig).before(bar);
    const byFig = new Map(cells.filter(c => c.kind !== 'hidden').map(c => [c.fig, c]));
    const nb = { lang: nbLang, cells, byFig };
    $('[data-nb-all]', bar).addEventListener('click', () => {
      const last = cells.filter(c => c.runAll && c.kind === 'main').pop();   // 在最后一个语句格上跑整篇
      if (last) last.runAll();
    });
    return nb;
  }

  $$('.md figure.highlight').forEach(setup);
  renderSummary();
})();
