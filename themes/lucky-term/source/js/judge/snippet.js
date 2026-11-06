/**
 * 文章代码块「跟练」：能在浏览器里运行的 C/C++ 代码块（判定规则见 snippet-core.js）在标题栏多出两个按钮
 *   run   编译运行，输出显示在代码块下面
 *   edit  打开编辑器，有四种练法：
 *           改一改  原代码随便改着跑
 *           填空    挖掉几行，补上后输出和原程序一致就算通过
 *           照着敲  原代码留在上面，在空编辑器里一行行敲出来；当前行用淡色提示后面的内容，敲错的行标红
 *           默写    原代码藏起来，自己写出输出一样的程序
 *         判定都是「输出和原程序一致」：原程序也在浏览器里跑一遍，输出里的地址不比；
 *         用到时间、随机数的程序输出不固定，只要求能正常运行。
 * 草稿按代码块和练法保存在本机，文章里的代码更新后作废；通过记录存在 lt-practice（首页动态、专栏页用）。
 * 编译器和做题页共用（ide.js / cpp-worker.js / wasi-worker.js），第一次运行时才下载。
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
    edit: { name: '改一改', tip: '随便改，改完点 run 看看结果有什么变化' },
    fill: { name: '填空', level: 1, tip: '补全挖掉的几行（行尾注释是提示），输出和原程序一致就算通过' },
    type: { name: '照着敲', level: 2, tip: '对着上面的代码一行行敲出来，注释可以不敲' },
    recall: { name: '默写', level: 3, tip: '不看原文，写出输出一样的程序' }
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
  // 照着敲时关掉自动补全括号和补全提示，每个字符都自己敲
  const TYPING_OFF = {
    autoClosingBrackets: 'never', autoClosingQuotes: 'never', autoSurround: 'never', autoClosingOvertype: 'never',
    quickSuggestions: false, suggestOnTriggerCharacters: false, wordBasedSuggestions: 'off', parameterHints: { enabled: false }
  };
  const TYPING_ON = {
    autoClosingBrackets: 'languageDefined', autoClosingQuotes: 'languageDefined', autoSurround: 'languageDefined', autoClosingOvertype: 'auto',
    quickSuggestions: true, suggestOnTriggerCharacters: true, wordBasedSuggestions: 'currentDocument', parameterHints: { enabled: true }
  };

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
    const info = S.classify(lang, original, opt);
    const head = $('.code-head', fig);
    if (!info.runnable || !head) return;

    const id = ++seq;
    const hash = S.hash(original);
    const draftKey = mode => `lt-snip:${PAGE}:${hash}${mode === 'edit' ? '' : ':' + mode}`;
    const scroll = $('.code-scroll', fig) || $('table', fig);
    const practicable = !info.error;       // 演示错误的代码只能改一改
    const unstable = S.unstable(original);
    const blankLines = practicable ? S.blanks(original, opt.blank) : [];
    const typing = S.typingLines(original);
    fig.classList.add('is-runnable');
    const canFill = blankLines.length > 0;  // 太短的程序（比如 hello world）挖不出空，不提供填空
    const snip = { fig, hash, practicable, canFill };
    snippets.push(snip);

    const acts = document.createElement('span');
    acts.className = 'code-acts';
    acts.innerHTML = `<span class="code-badge" data-badge hidden></span>`
      + `<button type="button" class="code-btn" data-snip-run title="编译运行（${MOD}+Enter）">▶ run</button>`
      + `<button type="button" class="code-btn" data-snip-edit title="改代码、填空、照着敲、默写">edit</button>`;
    head.insertBefore(acts, $('.code-copy', head));
    const runBtn = $('[data-snip-run]', acts), editBtn = $('[data-snip-edit]', acts), badge = $('[data-badge]', acts);

    let mode = null;                       // 打开编辑器时的练法
    let editor = null, m = null, edBox = null, bar = null, expectEl = null, fallback = null;
    const models = {};
    let busy = false, panel = null, ghostIds = [], typedOk = 0;

    function showBadge() {
      const lv = levelOf(recordOf(hash));
      badge.hidden = !lv;
      badge.textContent = lv ? `✓ ${LEVEL_NAME[lv]}` : '';
      badge.title = lv ? `已经通过「${LEVEL_NAME[lv]}」` : '';
      if (bar) $$('[data-mode]', bar).forEach(b => b.classList.toggle('is-passed', !!recordOf(hash)[b.dataset.mode]));
    }
    const hasDraft = () => Object.keys(MODES).some(k => store.get(draftKey(k)) !== null);
    const markDraft = () => { editBtn.classList.toggle('has-draft', !mode && hasDraft()); };
    showBadge();
    markDraft();

    // 每种练法的初始代码
    const initial = k => (k === 'fill' ? S.blankCode(original, blankLines) : k === 'type' || k === 'recall' ? '' : original);
    const code = () => (!mode ? original : editor ? editor.getValue() : fallback ? fallback.value : original);

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
        expected.set(stdin, build(original).then(c => {
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
        <label class="snip-stdin" hidden><span class="c-muted">标准输入（程序读到的内容，改完再点 run）</span><textarea rows="2" spellcheck="false" autocomplete="off" id="snip-stdin-${id}"></textarea></label>
        <div class="snip-verdict" data-verdict hidden></div>
        <pre class="snip-out" data-out></pre>`;
      fig.appendChild(panel);
      const ta = $('textarea', panel);
      ta.value = info.stdin;
      const toggle = $('[data-stdin-toggle]', panel);
      const showStdin = on => { $('.snip-stdin', panel).hidden = !on; toggle.setAttribute('aria-expanded', String(on)); toggle.classList.toggle('is-on', on); };
      toggle.addEventListener('click', () => { const on = $('.snip-stdin', panel).hidden; showStdin(on); if (on) ta.focus(); });
      ta.addEventListener('keydown', e => { if ((e.metaKey || e.ctrlKey) && e.key === 'Enter') { e.preventDefault(); run(); } });
      $('[data-close]', panel).addEventListener('click', () => { panel.remove(); panel = null; });
      if (info.readsStdin) showStdin(true);
      return panel;
    }
    const api = {
      status(text, cls) {
        const st = $('[data-st]', panel);
        st.className = 'snip-status' + (cls ? ' is-' + cls : '');
        st.innerHTML = (cls === 'busy' ? '<span class="snip-spin" aria-hidden="true"></span>' : '') + text;
      }
    };
    // 要读输入、作者又没给 stdin 的程序：第一次运行前先打开输入框
    function needStdin() {
      if (panel || !info.readsStdin || info.stdin) return false;
      ensurePanel();
      api.status(`这段程序会从键盘读取输入：在下面填好，再点 run（${MOD}+Enter）`);
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
    async function execute(src, stdin) {
      const o = $('[data-out]', panel);
      o.innerHTML = '';
      o.classList.remove('is-empty');
      mark([]);
      api.status(compilePhase(), 'busy');
      const c = await build(src);
      const diags = S.diagnostics(lang, c.stderr);
      mark(diags);
      // 作者标了 run: error 的代码本来就是演示错误的，没改过时出错给一句说明
      const demo = info.error && src === original ? ' <span class="c-muted">· 这段代码本来就是用来演示这个错误的</span>' : '';
      if (!c.ok) {
        api.status(c.loadError ? '✗ 编译器加载失败' : '✗ 编译出错' + demo, 'bad');
        o.innerHTML = `<span class="snip-err">${esc(IDE.cleanClang(c.stderr) || '编译失败')}</span>`;
        return { ok: false };
      }
      const warn = IDE.cleanClang(c.stderr);
      if (warn) o.innerHTML = `<details class="snip-warn"><summary>编译警告 ${diags.filter(d => d.severity === 'warning').length || ''} 条</summary>${esc(warn)}</details>`;
      api.status('运行中…', 'busy');
      const outNode = document.createTextNode('');
      o.appendChild(outNode);
      const t0 = performance.now();
      const r = await IDE.runWasm(CFG.wasiWorker, c.wasm, stdin, TIME_LIMIT, text => { outNode.data += text; });
      const ms = performance.now() - t0;
      outNode.data = r.stdout;
      if (r.stderr) o.insertAdjacentHTML('beforeend', `<span class="snip-err">${esc(r.stderr)}</span>`);
      if (r.timeout) {
        api.status(`✗ 超时：运行超过 ${TIME_LIMIT / 1000} 秒，已停止${demo || '（死循环，或者在等输入？）'}`, 'bad');
      } else if (r.error) {
        api.status('✗ 运行出错' + demo, 'bad');
        o.insertAdjacentHTML('beforeend', `<span class="snip-err">${esc(r.error)}</span>`);
      } else {
        api.status(`✓ 运行结束 · ${fmtMs(ms)}${r.exitCode ? ` · <span class="c-warn">退出码 ${r.exitCode}</span>${demo}` : ''}`, 'ok');
      }
      if (!r.stdout && !r.stderr && !r.error && !warn) { o.classList.add('is-empty'); o.textContent = '（没有输出）'; }
      return { ok: true, run: r };
    }

    async function guarded(fn) {
      if (busy) return;
      busy = true;
      runBtn.disabled = true;
      if (bar) $('[data-check]', bar).disabled = true;
      active = api;
      try {
        await fn();
      } catch (e) {
        api.status('✗ 出错了', 'bad');
        $('[data-out]', panel).textContent = String(e && e.stack || e);
      } finally {
        busy = false;
        runBtn.disabled = false;
        if (bar) $('[data-check]', bar).disabled = false;
      }
    }

    function run() {
      if (needStdin()) return;
      ensurePanel();
      $('[data-verdict]', panel).hidden = true;
      return guarded(() => execute(code(), stdinValue()));
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
        const mine = await execute(src, stdin);
        if (!mine.ok) { showVerdict(false, '编译没通过，先把错误改掉'); return; }
        const r = mine.run;
        if (r.timeout || r.error) { showVerdict(false, '程序没能正常运行完'); return; }
        if (unstable) {
          showVerdict(true, '这段程序每次运行输出都不一样（用到了时间或随机数），能正常运行就算通过');
          return;
        }
        api.status('运行原程序，对比输出…', 'busy');
        const exp = await expectedOutput(stdin);
        if (exp.error || exp.timeout) { showVerdict(false, '原程序没能运行，无法对比：' + (exp.error || '超时')); return; }
        const cmp = S.compareOutput(r.stdout, exp.stdout);
        api.status(cmp.ok ? '✓ 运行结束' : '✓ 运行结束，但输出和原程序不一样', cmp.ok ? 'ok' : 'bad');
        if (cmp.ok) showVerdict(true, '输出和原程序完全一致');
        else showVerdict(false, `输出从第 ${cmp.line} 行开始不一样`, cmp, exp.stdout);
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
        const next = { fill: 'type', type: 'recall' }[mode];
        if (next) html += ` <button type="button" class="link-btn" data-next="${next}">接着试试「${MODES[next].name}」→</button>`;
        record(hash, mode);
        showBadge();
        renderSummary();
      }
      v.innerHTML = html;
      const nb = $('[data-next]', v);
      if (nb) nb.addEventListener('click', () => switchMode(nb.dataset.next));
    }

    /* --- 编辑器和练法 --- */
    function tip(text, cls) {
      if (!bar) return;
      const t = $('[data-tip]', bar);
      t.textContent = text;
      t.className = 'snip-tip' + (cls ? ' is-' + cls : '');
    }
    function defaultTip() {
      if (mode === 'type') return updateTyping();
      let t = MODES[mode].tip;
      if (unstable && mode !== 'edit') t += '。这段程序输出不固定，能正常运行就算通过';
      tip(t);
    }

    function buildBar() {
      bar = document.createElement('div');
      bar.className = 'snip-modes';
      const tabs = Object.keys(MODES).filter(k => k === 'edit' || (practicable && (k !== 'fill' || canFill)))
        .map(k => `<button type="button" class="snip-tab" data-mode="${k}" role="tab">${MODES[k].name}</button>`).join('');
      bar.innerHTML = `
        <div class="snip-tabs" role="tablist" aria-label="练法">${tabs}</div>
        <div class="snip-mode-row">
          <span class="snip-tip" data-tip></span>
          <span class="snip-mode-acts">
            <button type="button" class="code-btn" data-peek hidden>偷看原文</button>
            <button type="button" class="code-btn" data-expect hidden>期望输出</button>
            <button type="button" class="code-btn" data-reset title="恢复成这种练法的初始代码">reset</button>
            <button type="button" class="btn btn-solid btn-sm snip-check" data-check hidden>提交检查</button>
          </span>
        </div>`;
      expectEl = document.createElement('div');
      expectEl.className = 'snip-expect';
      expectEl.hidden = true;
      bar.appendChild(expectEl);
      head.after(bar);
      bar.addEventListener('click', e => {
        const t = e.target.closest('[data-mode]');
        if (t) switchMode(t.dataset.mode);
      });
      $('[data-check]', bar).addEventListener('click', check);
      $('[data-reset]', bar).addEventListener('click', reset);
      $('[data-peek]', bar).addEventListener('click', e => {
        const on = scroll.hidden;
        scroll.hidden = !on;
        e.currentTarget.classList.toggle('is-on', on);
        e.currentTarget.textContent = on ? '藏起原文' : '偷看原文';
      });
      $('[data-expect]', bar).addEventListener('click', async e => {
        const btn = e.currentTarget;
        if (!expectEl.hidden) { expectEl.hidden = true; btn.classList.remove('is-on'); return; }
        btn.classList.add('is-on');
        expectEl.hidden = false;
        if (info.readsStdin && !stdinValue()) { expectEl.innerHTML = '<span class="c-muted">这段程序要读输入：先在输出面板的 stdin 里填好输入。</span>'; ensurePanel(); $('.snip-stdin', panel).hidden = false; return; }
        expectEl.innerHTML = `<span class="c-muted"><span class="snip-spin" aria-hidden="true"></span> 运行原程序…</span>`;
        active = { status: t => { expectEl.innerHTML = `<span class="c-muted">${t}</span>`; } };
        const r = await expectedOutput(stdinValue());
        if (expectEl.hidden) return;
        expectEl.innerHTML = r.error || r.timeout
          ? `<span class="snip-err">原程序没能运行：${esc(r.error || '超时')}</span>`
          : `<div class="c-muted">原程序的输出${unstable ? '（每次运行都不一样）' : ''}：</div><pre>${esc(r.stdout) || '（没有输出）'}</pre>`;
      });
    }

    function saveDraft() {
      if (!mode) return;
      const v = code();
      if (v === initial(mode)) store.del(draftKey(mode)); else store.set(draftKey(mode), v);
    }

    async function open(k) {
      mode = k;
      editBtn.textContent = 'done';
      editBtn.title = '收起编辑器（改动会保留）';
      fig.classList.add('is-editing');
      markDraft();
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
      edBox.hidden = false;
      applyMode();
      if (editor || fallback) return;
      try {
        const mm = await monaco();
        m = mm;
        edBox.innerHTML = '';
        editor = mm.editor.create(edBox, Object.assign({ model: modelFor(mode) }, IDE.editorOptions(), { lineNumbersMinChars: 3 }, mode === 'type' ? TYPING_OFF : {}));
        editor.addCommand(mm.KeyMod.CtrlCmd | mm.KeyCode.Enter, run);
        editor.addCommand(mm.KeyMod.CtrlCmd | mm.KeyMod.Shift | mm.KeyCode.Enter, check);
        editor.onDidContentSizeChange(fit);
        editor.onDidChangeCursorPosition(() => { if (mode === 'type') updateTyping(); });
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
        fallback.addEventListener('input', saveDraft);
        fallback.addEventListener('keydown', e => { if ((e.metaKey || e.ctrlKey) && e.key === 'Enter') { e.preventDefault(); (e.shiftKey ? check : run)(); } });
        fallback.focus();
      }
    }

    function modelFor(k) {
      if (models[k]) return models[k];
      const model = m.editor.createModel(store.get(draftKey(k)) ?? initial(k), lang === 'c' ? 'c' : 'cpp');
      let timer;
      model.onDidChangeContent(() => {
        clearTimeout(timer);
        timer = setTimeout(saveDraft, 400);
        m.editor.setModelMarkers(model, 'snippet', []);
        if (mode === 'type' && editor && editor.getModel() === model) updateTyping();
      });
      return (models[k] = model);
    }

    // 切换练法：换编辑器内容、显示或隐藏原代码
    function applyMode() {
      $$('[data-mode]', bar).forEach(b => { const on = b.dataset.mode === mode; b.classList.toggle('is-on', on); b.setAttribute('aria-selected', String(on)); });
      const practice = mode !== 'edit';
      $('[data-check]', bar).hidden = !practice;
      $('[data-peek]', bar).hidden = mode !== 'recall';
      $('[data-peek]', bar).textContent = '偷看原文';
      $('[data-peek]', bar).classList.remove('is-on');
      $('[data-expect]', bar).hidden = !(mode === 'fill' || mode === 'recall');
      $('[data-expect]', bar).classList.remove('is-on');
      expectEl.hidden = true;
      scroll.hidden = mode !== 'type';           // 照着敲时原代码留在上面
      showBadge();
      if (panel) $('[data-verdict]', panel).hidden = true;
      if (editor) {
        ghostIds = editor.deltaDecorations(ghostIds, []);
        editor.setModel(modelFor(mode));
        editor.updateOptions(mode === 'type' ? TYPING_OFF : TYPING_ON);
        if (mode === 'fill') highlightBlanks();
        fit();
        defaultTip();
        editor.focus();
      } else if (fallback) {
        fallback.value = store.get(draftKey(mode)) ?? initial(mode);
        defaultTip();
      } else defaultTip();
    }
    function switchMode(k) {
      if (k === mode) return;
      saveDraft();
      mode = k;
      applyMode();
    }

    // 填空：把挖掉的行高亮出来
    function highlightBlanks() {
      const model = editor.getModel();
      const ds = [];
      for (let i = 1; i <= model.getLineCount(); i++) {
        if (model.getLineContent(i).includes(S.BLANK_MARK)) ds.push({ range: new m.Range(i, 1, i, 1), options: { isWholeLine: true, className: 'snip-line-blank', linesDecorationsClassName: 'snip-gut-blank' } });
      }
      ghostIds = editor.deltaDecorations(ghostIds, ds);
    }

    // 照着敲：逐行和原文比对；光标所在行用淡色显示这一行还没敲的部分
    function updateTyping() {
      if (!editor || mode !== 'type') return;
      const model = editor.getModel();
      const pos = editor.getPosition();
      const ds = [];
      let k = 0, ok = 0, bad = 0, ghost = null;
      const n = model.getLineCount();
      for (let i = 1; i <= n; i++) {
        const text = model.getLineContent(i);
        const norm = S.normLine(text);
        const exp = typing[k];
        const atCursor = pos && pos.lineNumber === i;
        if (atCursor && exp) {
          const typed = S.splitComment(text).code;
          const at = S.matchPrefix(exp.raw, typed);
          if (at >= 0 && norm !== exp.norm) {
            // 正在敲这一行，前面都对：提示剩下的部分
            if (pos.column === model.getLineMaxColumn(i)) ghost = { line: i, text: exp.raw.slice(at) };
            continue;
          }
        }
        if (!norm) continue;
        if (!exp) {
          bad++;
          ds.push({ range: new m.Range(i, 1, i, 1), options: { isWholeLine: true, className: 'snip-line-bad', linesDecorationsClassName: 'snip-gut-bad', hoverMessage: { value: '原文已经敲完了，这一行是多出来的' } } });
        } else if (norm === exp.norm) {
          ok++;
          ds.push({ range: new m.Range(i, 1, i, 1), options: { isWholeLine: true, linesDecorationsClassName: 'snip-gut-ok' } });
        } else {
          bad++;
          ds.push({ range: new m.Range(i, 1, i, 1), options: { isWholeLine: true, className: 'snip-line-bad', linesDecorationsClassName: 'snip-gut-bad', hoverMessage: { value: '原文：`' + exp.raw.replace(/`/g, '\\`') + '`' } } });
        }
        k++;
      }
      // 光标在空行上：提示下一行原文
      if (!ghost && pos && typing[k] && !model.getLineContent(pos.lineNumber).trim()) ghost = { line: pos.lineNumber, text: typing[k].raw };
      if (ghost) {
        const col = model.getLineMaxColumn(ghost.line);
        ds.push({ range: new m.Range(ghost.line, col, ghost.line, col), options: { after: { content: ghost.text, inlineClassName: 'snip-ghost' }, showIfCollapsed: true } });
      }
      ghostIds = editor.deltaDecorations(ghostIds, ds);
      typedOk = ok;
      const total = typing.length;
      if (ok === total && !bad) tip(`全部 ${total} 行都敲对了 ✓ 点「提交检查」运行一下`, 'ok');
      else tip(`已敲对 ${ok} / ${total} 行${bad ? `，${bad} 行和原文不一样（鼠标停在红色行上看原文）` : ''}。注释可以不敲`, bad ? 'bad' : '');
    }

    const fit = () => {
      if (!editor) return;
      const h = Math.min(Math.max(editor.getContentHeight(), Math.min(lines.length * 22 + 24, 240), 120), Math.round(window.innerHeight * 0.7));
      edBox.style.height = h + 'px';
    };

    function close() {
      saveDraft();
      mode = null;
      editBtn.textContent = 'edit';
      editBtn.title = '改代码、填空、照着敲、默写';
      fig.classList.remove('is-editing');
      bar.hidden = true;
      edBox.hidden = true;
      scroll.hidden = false;
      if (panel) $('[data-verdict]', panel).hidden = true;
      markDraft();
    }
    function reset() {
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

    runBtn.addEventListener('click', run);
    editBtn.addEventListener('click', () => (mode ? close() : open('edit')));
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

  $$('.md figure.highlight').forEach(setup);
  renderSummary();
})();
