/**
 * 文章代码块「跟练」：能在浏览器里运行的 C/C++ 代码块（判定规则见 snippet-core.js）在标题栏多出两个按钮
 *   run   编译运行，输出显示在代码块下面
 *   edit  把代码块换成编辑器，改完直接运行；改动按代码块保存在本机，文章里的代码更新后旧草稿作废
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
    del(k) { try { localStorage.removeItem(k); } catch (e) { /* ignore */ } }
  };

  const S = window.LTSnippet, IDE = window.LTIde, CFG = window.LT_SNIPPET;
  const TIME_LIMIT = 10000;
  const isMac = /Mac|iPhone|iPad/.test(navigator.platform);
  const MOD = isMac ? '⌘' : 'Ctrl';
  const fmtMs = ms => (ms < 1 ? '< 1' : Math.round(ms)) + ' ms';

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

  let seq = 0;
  function setup(fig) {
    const lang = S.normLang((fig.className.replace('highlight', '').trim() || '').split(/\s+/)[0]);
    if (!lang) return;
    const lines = $$('.code .line', fig).map(l => l.textContent);
    const original = lines.length ? lines.join('\n') : ($('code', fig) || fig).textContent;
    const info = S.classify(lang, original, S.parseOptions(optionComment(fig)));
    const head = $('.code-head', fig);
    if (!info.runnable || !head) return;

    const id = ++seq;
    const key = `lt-snip:${location.pathname}:${S.hash(original)}`;
    const scroll = $('.code-scroll', fig) || $('table', fig);
    fig.classList.add('is-runnable');

    const acts = document.createElement('span');
    acts.className = 'code-acts';
    acts.innerHTML = `<button type="button" class="code-btn" data-snip-run title="编译运行（${MOD}+Enter）">▶ run</button>`
      + `<button type="button" class="code-btn" data-snip-edit title="在这里修改代码再运行">edit</button>`
      + `<button type="button" class="code-btn" data-snip-reset title="恢复成文章里的代码" hidden>reset</button>`;
    const copy = $('.code-copy', head);
    head.insertBefore(acts, copy);
    const runBtn = $('[data-snip-run]', acts), editBtn = $('[data-snip-edit]', acts), resetBtn = $('[data-snip-reset]', acts);

    let editor = null, model = null, editing = false, busy = false, panel = null, edBox = null, lastWasm = { src: null };
    const draft = () => store.get(key);
    const markDraft = () => { editBtn.classList.toggle('has-draft', !!draft() && !editing); editBtn.title = draft() && !editing ? '你改过这段代码，点开继续改' : '在这里修改代码再运行'; };
    markDraft();
    const code = () => (editing && model ? model.getValue() : editing ? $('textarea', edBox).value : original);

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
    const out = () => $('[data-out]', panel);

    function mark(diags) {
      if (!model || !window.monaco) return;
      const m = window.monaco;
      m.editor.setModelMarkers(model, 'snippet', diags.filter(d => d.line > 0 && d.line <= model.getLineCount()).map(d => {
        const word = model.getWordAtPosition({ lineNumber: d.line, column: Math.max(1, d.col) });
        return {
          startLineNumber: d.line, startColumn: word ? word.startColumn : Math.max(1, d.col),
          endLineNumber: d.line, endColumn: word ? word.endColumn : model.getLineMaxColumn(d.line),
          message: d.msg, severity: d.severity === 'warning' ? m.MarkerSeverity.Warning : m.MarkerSeverity.Error
        };
      }));
    }

    async function run() {
      if (busy) return;
      // 要读输入、作者又没给 stdin 的程序：第一次点 run 先打开输入框，填好再运行
      if (!panel && info.readsStdin && !info.stdin) {
        ensurePanel();
        api.status(`这段程序会从键盘读取输入：在下面填好，再点 run（${MOD}+Enter）`);
        $('textarea', panel).focus();
        return;
      }
      busy = true;
      runBtn.disabled = true;
      ensurePanel();
      active = api;
      const src = code();
      const stdin = $('textarea', panel).value;
      const o = out();
      o.innerHTML = '';
      o.classList.remove('is-empty');
      mark([]);
      try {
        let c = lastWasm;
        if (lastWasm.src !== src) {
          api.status(cc.isReady() ? '编译中…' : '首次运行：下载编译器（约 23MB，之后会缓存）…', 'busy');
          c = await cc.compile(S.compileArgs(lang, info.flags), S.compileFiles(lang, src));
          c.src = src;
          if (c.ok) lastWasm = c;
        }
        const diags = S.diagnostics(lang, c.stderr);
        mark(diags);
        // 作者标了 run: error 的代码本来就是演示错误的，没改过时出错给一句说明
        const demo = info.error && src === original ? ' <span class="c-muted">· 这段代码本来就是用来演示这个错误的</span>' : '';
        if (!c.ok) {
          api.status(c.loadError ? '✗ 编译器加载失败' : '✗ 编译出错' + demo, 'bad');
          o.innerHTML = `<span class="snip-err">${esc(IDE.cleanClang(c.stderr) || '编译失败')}</span>`;
          return;
        }
        const warn = IDE.cleanClang(c.stderr);
        if (warn) o.innerHTML = `<details class="snip-warn"><summary>编译警告 ${diags.filter(d => d.severity === 'warning').length || ''} 条</summary>${esc(warn)}</details>`;
        api.status('运行中…', 'busy');
        const outNode = document.createTextNode('');
        o.appendChild(outNode);
        const t0 = performance.now();
        // postMessage 会复制一份 wasm，编译结果留着，代码没改时再点 run 不用重新编译
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
      } catch (e) {
        api.status('✗ 出错了', 'bad');
        o.textContent = String(e && e.stack || e);
      } finally {
        busy = false;
        runBtn.disabled = false;
      }
    }

    /* --- 编辑 --- */
    const LINE = 22;
    const fit = () => {
      if (!editor) return;
      const h = Math.min(Math.max(editor.getContentHeight(), 120), Math.round(window.innerHeight * 0.7));
      edBox.style.height = h + 'px';
    };
    async function startEdit() {
      editing = true;
      editBtn.textContent = 'done';
      editBtn.title = '收起编辑器，回到文章里的代码（改动会保留）';
      resetBtn.hidden = false;
      fig.classList.add('is-editing');
      cc.preload();                          // 开始改代码时就在后台准备编译器
      const text = draft() ?? original;
      if (!edBox) {
        edBox = document.createElement('div');
        edBox.className = 'snip-editor';
        edBox.style.height = Math.min(lines.length * LINE + 24, window.innerHeight * 0.7) + 'px';
        edBox.innerHTML = '<div class="snip-loading c-muted">加载编辑器…</div>';
        scroll.after(edBox);
      }
      scroll.hidden = true;
      edBox.hidden = false;
      if (editor) { editor.focus(); return; }
      try {
        const m = await monaco();
        model = m.editor.createModel(text, lang === 'c' ? 'c' : 'cpp');
        edBox.innerHTML = '';
        editor = m.editor.create(edBox, Object.assign({ model }, IDE.editorOptions(), { lineNumbersMinChars: 3, scrollBeyondLastLine: false }));
        editor.addCommand(m.KeyMod.CtrlCmd | m.KeyCode.Enter, run);
        editor.onDidContentSizeChange(fit);
        let timer;
        model.onDidChangeContent(() => {
          clearTimeout(timer);
          timer = setTimeout(save, 400);
          m.editor.setModelMarkers(model, 'snippet', []);
        });
        fit();
        editor.focus();
      } catch (e) {
        // 编辑器加载失败（比如 CDN 被墙）：退回普通文本框，照样能改能运行
        edBox.innerHTML = '<textarea class="snip-fallback" spellcheck="false"></textarea>';
        edBox.style.height = 'auto';
        const ta = $('textarea', edBox);
        ta.value = text;
        ta.rows = Math.min(lines.length + 1, 30);
        ta.addEventListener('input', save);
        ta.addEventListener('keydown', e => { if ((e.metaKey || e.ctrlKey) && e.key === 'Enter') { e.preventDefault(); run(); } });
        ta.focus();
      }
    }
    function save() {
      const v = code();
      if (v === original) store.del(key); else store.set(key, v);
    }
    function stopEdit() {
      save();
      editing = false;
      editBtn.textContent = 'edit';
      resetBtn.hidden = true;
      fig.classList.remove('is-editing');
      edBox.hidden = true;
      scroll.hidden = false;
      markDraft();
    }
    function reset() {
      if (!confirm(`把这段代码恢复成文章里的原样？${model ? `（之后可以用 ${MOD}+Z 撤销）` : ''}`)) return;
      if (editor) {
        editor.pushUndoStop();
        model.pushEditOperations([], [{ range: model.getFullModelRange(), text: original }], () => null);
        editor.pushUndoStop();
        editor.focus();
      } else if (edBox) $('textarea', edBox).value = original;
      store.del(key);
    }

    runBtn.addEventListener('click', run);
    editBtn.addEventListener('click', () => (editing ? stopEdit() : startEdit()));
    resetBtn.addEventListener('click', reset);
  }

  $$('.md figure.highlight').forEach(setup);
})();
