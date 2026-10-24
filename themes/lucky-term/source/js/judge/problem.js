/* 做题页：Monaco 编辑器、测试用例、运行和提交（代码在浏览器里编译运行，见 lang.js 和各个 *-worker.js） */
(function () {
  'use strict';

  const $ = (s, el = document) => el.querySelector(s);
  const $$ = (s, el = document) => Array.from(el.querySelectorAll(s));
  const esc = s => String(s).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  const store = {
    get(k, d) { try { const v = localStorage.getItem(k); return v === null ? d : JSON.parse(v); } catch (e) { return d; } },
    set(k, v) { try { localStorage.setItem(k, JSON.stringify(v)); } catch (e) { /* 隐私模式下忽略 */ } },
    del(k) { try { localStorage.removeItem(k); } catch (e) { /* ignore */ } }
  };

  const J = window.LTJudge;
  const A = window.LT_JUDGE;
  const P = JSON.parse($('#problem-data').textContent);
  const sig = P.sig;
  const root = $('[data-ide]');

  const KEY_STATE = 'lt-pb-state';                   // { slug: { status: 'ac' | 'tried', at, lang } }
  const KEY_LANG = 'lt-pb-lang';
  const KEY_LAYOUT = 'lt-pb-layout';
  const KEY_CASES = `lt-pb-cases:${P.slug}`;
  const codeKey = l => `lt-pb-code:${P.slug}:${l}`;

  const MONACO = 'https://cdn.jsdelivr.net/npm/monaco-editor@0.52.2/min';
  const isMac = /Mac|iPhone|iPad/.test(navigator.platform);
  const MOD = isMac ? '⌘' : 'Ctrl';
  // 一次运行（所有用例一起跑）的时间限制；Python 慢，给得宽一些
  const limit = l => (P.timeLimit || 3000) * (l === 'python' ? 3 : 1);

  let lang = store.get(KEY_LANG, 'cpp');
  if (!J.LANGS[lang]) lang = 'cpp';

  /* ---------- 状态栏 ---------- */
  const statusEl = $('[data-status]');
  const setStatus = t => { statusEl.textContent = t || ''; };

  /* ---------- 编辑器 ---------- */
  let monaco = null, editor = null;
  const models = {};
  const codeOf = l => store.get(codeKey(l), null) ?? J.template(l, sig);
  const getCode = () => (editor ? models[lang].getValue() : codeOf(lang));

  function loadMonaco() {
    return new Promise((resolve, reject) => {
      const s = document.createElement('script');
      s.src = MONACO + '/vs/loader.js';
      s.onerror = reject;
      s.onload = () => {
        // 编辑器的 Worker 跨域加载：用同源的 blob 包一层
        window.MonacoEnvironment = {
          getWorkerUrl: () => URL.createObjectURL(new Blob(
            [`self.MonacoEnvironment={baseUrl:'${MONACO}/'};importScripts('${MONACO}/vs/base/worker/workerMain.js');`],
            { type: 'text/javascript' }))
        };
        window.require.config({ paths: { vs: MONACO + '/vs' }, 'vs/nls': { availableLanguages: { '*': 'zh-cn' } } });
        window.require(['vs/editor/editor.main'], () => resolve(window.monaco), reject);
      };
      document.head.appendChild(s);
    });
  }

  // 从当前配色方案的 CSS 变量生成编辑器主题
  function cssHex(name) {
    const el = document.createElement('span');
    el.style.color = `var(${name})`;
    document.body.appendChild(el);
    const c = getComputedStyle(el).color;
    el.remove();
    const n = (c.match(/[\d.]+/g) || ['0', '0', '0']).map(Number);
    const rgb = /^color\(/.test(c) ? n.slice(0, 3).map(x => Math.round(x * 255)) : n.slice(0, 3);
    return '#' + rgb.map(x => Math.max(0, Math.min(255, x)).toString(16).padStart(2, '0')).join('');
  }
  function applyTheme() {
    if (!monaco) return;
    const c = {};
    ['--panel', '--panel-2', '--fg', '--fg-2', '--muted', '--border', '--border-2', '--accent', '--danger', '--warn',
      '--hl-comment', '--hl-keyword', '--hl-type', '--hl-string', '--hl-number', '--hl-func', '--hl-meta']
      .forEach(k => { c[k] = cssHex(k); });
    const t = v => v.slice(1);
    monaco.editor.defineTheme('lt', {
      base: document.documentElement.getAttribute('data-theme') === 'dark' ? 'vs-dark' : 'vs',
      inherit: true,
      rules: [
        { token: '', foreground: t(c['--fg']) },
        { token: 'comment', foreground: t(c['--hl-comment']), fontStyle: 'italic' },
        { token: 'keyword', foreground: t(c['--hl-keyword']) },
        { token: 'keyword.directive', foreground: t(c['--hl-meta']) },
        { token: 'string', foreground: t(c['--hl-string']) },
        { token: 'string.escape', foreground: t(c['--hl-number']) },
        { token: 'regexp', foreground: t(c['--hl-string']) },
        { token: 'number', foreground: t(c['--hl-number']) },
        { token: 'type', foreground: t(c['--hl-type']) },
        { token: 'type.identifier', foreground: t(c['--hl-type']) },
        { token: 'annotation', foreground: t(c['--hl-meta']) },
        { token: 'predefined', foreground: t(c['--hl-func']) },
        { token: 'delimiter', foreground: t(c['--fg-2']) }
      ],
      colors: {
        'editor.background': c['--panel'],
        'editor.foreground': c['--fg'],
        'editorLineNumber.foreground': c['--muted'] + '99',
        'editorLineNumber.activeForeground': c['--fg'],
        'editor.lineHighlightBackground': c['--panel-2'],
        'editor.lineHighlightBorder': c['--panel-2'],
        'editor.selectionBackground': c['--accent'] + '40',
        'editor.inactiveSelectionBackground': c['--accent'] + '26',
        'editorCursor.foreground': c['--accent'],
        'editorIndentGuide.background1': c['--border'],
        'editorIndentGuide.activeBackground1': c['--border-2'],
        'editorWhitespace.foreground': c['--border-2'],
        'editorWidget.background': c['--panel'],
        'editorWidget.border': c['--border-2'],
        'editorSuggestWidget.background': c['--panel'],
        'editorSuggestWidget.border': c['--border-2'],
        'editorSuggestWidget.selectedBackground': c['--panel-2'],
        'editorHoverWidget.background': c['--panel'],
        'editorHoverWidget.border': c['--border-2'],
        'editorError.foreground': c['--danger'],
        'editorWarning.foreground': c['--warn'],
        'scrollbarSlider.background': c['--border-2'] + '80',
        'scrollbarSlider.hoverBackground': c['--border-2'],
        'scrollbar.shadow': '#00000000'
      }
    });
    monaco.editor.setTheme('lt');
  }

  async function initEditor() {
    const box = $('[data-editor]');
    try {
      monaco = await loadMonaco();
    } catch (e) {
      $('.ide-loading', box).innerHTML = '<span class="c-danger">编辑器加载失败，请检查网络后刷新页面。</span>';
      return;
    }
    await (document.fonts ? document.fonts.ready : Promise.resolve());
    applyTheme();
    new MutationObserver(applyTheme).observe(document.documentElement, { attributes: true, attributeFilter: ['data-scheme'] });

    // JavaScript 的补全知道 ListNode / TreeNode
    monaco.languages.typescript.javascriptDefaults.addExtraLib(`
declare class ListNode { constructor(val?: number, next?: ListNode | null); val: number; next: ListNode | null; }
declare class TreeNode { constructor(val?: number, left?: TreeNode | null, right?: TreeNode | null); val: number; left: TreeNode | null; right: TreeNode | null; }
`, 'judge.d.ts');

    Object.keys(J.LANGS).forEach(l => {
      const m = monaco.editor.createModel(codeOf(l), J.LANGS[l].monaco, monaco.Uri.parse(`file:///${J.LANGS[l].file}`));
      let timer;
      m.onDidChangeContent(() => {
        clearTimeout(timer);
        timer = setTimeout(() => store.set(codeKey(l), m.getValue()), 400);
        monaco.editor.setModelMarkers(m, 'judge', []);
      });
      models[l] = m;
    });

    box.innerHTML = '';
    editor = monaco.editor.create(box, {
      model: models[lang],
      theme: 'lt',
      automaticLayout: true,
      fontFamily: getComputedStyle(document.documentElement).getPropertyValue('--mono').trim(),
      fontSize: 14,
      lineHeight: 22,
      fontLigatures: false,
      tabSize: 4,
      insertSpaces: true,
      minimap: { enabled: false },
      scrollBeyondLastLine: false,
      padding: { top: 12, bottom: 12 },
      renderLineHighlight: 'all',
      bracketPairColorization: { enabled: true },
      guides: { bracketPairs: 'active', indentation: true },
      smoothScrolling: true,
      cursorBlinking: 'smooth',
      cursorSmoothCaretAnimation: 'on',
      stickyScroll: { enabled: false },
      fixedOverflowWidgets: true,
      scrollbar: { verticalScrollbarSize: 10, horizontalScrollbarSize: 10 }
    });
    editor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.Enter, () => execute('run'));
    editor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyMod.Shift | monaco.KeyCode.Enter, () => execute('submit'));
    editor.addCommand(monaco.KeyMod.CtrlCmd | monaco.KeyCode.KeyS, () => { store.set(codeKey(lang), getCode()); setStatus('已保存到本机'); });
    // 开始改代码时才在后台准备编译器，只是打开页面看看的不用下载
    editor.onDidChangeModelContent(preload);
    if (matchMedia('(min-width: 901px)').matches) editor.focus();
  }

  /* ---------- 语言切换和重置 ---------- */
  const langSel = $('[data-lang]');
  langSel.innerHTML = Object.keys(J.LANGS).map(l => `<option value="${l}">${J.LANGS[l].name}</option>`).join('');
  langSel.value = lang;
  $('[data-file]').textContent = J.LANGS[lang].file;
  langSel.addEventListener('change', () => {
    lang = langSel.value;
    store.set(KEY_LANG, lang);
    if (editor) { editor.setModel(models[lang]); editor.focus(); }
    $('[data-file]').textContent = J.LANGS[lang].file;
    setStatus('');
  });

  $('[data-reset]').addEventListener('click', () => {
    if (!confirm(`把 ${J.LANGS[lang].name} 代码恢复为初始模板？（之后可以用 ${MOD}+Z 撤销）`)) return;
    const tpl = J.template(lang, sig);
    if (editor) {
      const m = models[lang];
      editor.pushUndoStop();
      m.pushEditOperations([], [{ range: m.getFullModelRange(), text: tpl }], () => null);
      editor.pushUndoStop();
    }
    store.del(codeKey(lang));
  });

  /* ---------- 运行器 ---------- */

  // 跑一次一次性的 Worker：超时直接 terminate；标准输出边跑边收，超时也知道跑到第几个用例
  function runInWorker(url, msg, ms, opts) {
    return new Promise(resolve => {
      const w = new Worker(url, opts);
      let stdout = '';
      const finish = r => { clearTimeout(timer); w.terminate(); resolve(Object.assign({ stdout }, r)); };
      const timer = setTimeout(() => finish({ timeout: true }), ms);
      w.onmessage = ({ data }) => {
        if (data.type === 'out') stdout += data.text;
        else if (data.type === 'done') {
          finish({
            stderr: data.stderr || '',
            runtimeError: data.error ? explainTrap(data.error) : data.exitCode ? `程序以退出码 ${data.exitCode} 结束` : null
          });
        }
      };
      w.onerror = e => { e.preventDefault(); finish({ runtimeError: e.message || '运行失败' }); };
      w.postMessage(msg);
    });
  }

  function explainTrap(msg) {
    const hints = [
      [/unreachable/i, '程序异常终止：可能是 abort()、assert 失败、vector::at 越界或抛出了异常（浏览器里的 C++ 不支持异常）'],
      [/out of bounds|memory access/i, '内存越界访问：检查数组下标、空指针和迭代器'],
      [/call stack|stack size|too much recursion/i, '递归太深，栈溢出'],
      [/divide by zero|division by zero|remainder by zero/i, '整数除以零'],
      [/out of memory|could not allocate|allocation failed/i, '内存超出限制']
    ];
    const h = hints.find(x => x[0].test(msg));
    return h ? `${msg}\n${h[1]}` : msg;
  }

  // C++：常驻的编译 Worker + 每次新建的运行 Worker
  const CLANG_PKG = `${J.CLANG.pkg}@${J.CLANG.version}`;
  let cppW = null, cppReady = false, cppSeq = 0, lastBuild = { src: null, wasm: null };
  const cppWaiting = {};
  function cppWorker() {
    if (cppW) return cppW;
    cppW = new Worker(A.cppWorker, { type: 'module' });
    cppW.onmessage = ({ data }) => {
      if (data.type === 'progress' && data.total) {
        if (!cppReady) setStatus(`下载 C++ 编译器 ${Math.min(99, Math.floor(data.done / data.total * 100))}%`);
      } else if (data.type === 'ready') {
        cppReady = data.ok;
        setStatus(data.ok ? 'clang 22 就绪' : 'C++ 编译器加载失败');
      } else if (data.type === 'compiled') {
        cppReady = true;
        const done = cppWaiting[data.id];
        delete cppWaiting[data.id];
        if (done) done(data);
      }
    };
    cppW.onerror = e => {
      e.preventDefault();
      Object.keys(cppWaiting).forEach(id => { cppWaiting[id]({ ok: false, stderr: 'C++ 编译器加载失败：' + (e.message || '请检查网络') }); delete cppWaiting[id]; });
      cppW = null;
    };
    return cppW;
  }
  function compileCpp(source) {
    if (lastBuild.src === source) return Promise.resolve({ ok: true, wasm: lastBuild.wasm, stderr: lastBuild.stderr });
    return new Promise(resolve => {
      const id = ++cppSeq;
      cppWaiting[id] = resolve;
      cppWorker().postMessage({ type: 'compile', id, pkg: CLANG_PKG, args: ['clang++', ...J.CPP_FLAGS, 'main.cpp', '-o', 'main.wasm'], files: J.cppFiles(source) });
    }).then(r => {
      if (r.ok) lastBuild = { src: source, wasm: r.wasm, stderr: r.stderr };
      return r;
    });
  }
  async function runCpp(code, cases, phase) {
    phase(cppReady ? '编译中…' : '首次使用：下载 C++ 编译器（约 23MB，之后会缓存）…');
    const c = await compileCpp(J.cppProgram(sig, code));
    if (!c.ok) return { compileError: cleanClang(c.stderr) || '编译失败', diagnostics: cppDiagnostics(c.stderr) };
    setStatus('clang 22 就绪');
    phase('运行中…');
    const r = await runInWorker(A.wasiWorker, { wasm: c.wasm, stdin: J.encodeInput(cases) }, limit('cpp'));
    return Object.assign(r, { diagnostics: cppDiagnostics(c.stderr) });
  }
  function cppDiagnostics(text) {
    const out = [];
    const re = /^solution\.cpp:(\d+):(\d+): (error|warning): (.*)$/gm;
    let m;
    while ((m = re.exec(text || ''))) out.push({ line: +m[1], col: +m[2], severity: m[3], msg: m[4] });
    return out;
  }
  function cleanClang(text) {
    text = (text || '').replace(/\x1b\[[0-9;]*m/g, '').trim();
    if (/lt_judge\.cpp/.test(text) && !/solution\.cpp:\d+:\d+: error/.test(text)) {
      text = `提示：判题程序调用 Solution::${sig.function} 时出错，请检查类名、函数名、参数和返回类型是否和模板一致。\n\n` + text;
    }
    return text;
  }

  // Python：常驻 Worker（加载 Pyodide 比较慢），超时才销毁重建
  let pyW = null, pyReady = false;
  function pyWorker() {
    if (pyW) return pyW;
    pyW = new Worker(A.pyWorker);
    pyW.addEventListener('message', ({ data }) => {
      if (data.type === 'ready') { pyReady = true; setStatus('Python 3.12 就绪'); }
    });
    return pyW;
  }
  function runPython(code, cases, phase) {
    phase(pyReady ? '运行中…' : '加载 Python 运行环境（约 10MB，之后会缓存）…');
    return new Promise(resolve => {
      const w = pyWorker();
      let stdout = '', timer = null;
      const finish = r => {
        clearTimeout(timer);
        w.removeEventListener('message', onMsg);
        w.removeEventListener('error', onErr);
        resolve(Object.assign({ stdout }, r));
      };
      const kill = () => { w.terminate(); pyW = null; pyReady = false; };
      const onMsg = ({ data }) => {
        if (data.type === 'start') { phase('运行中…'); timer = setTimeout(() => { kill(); finish({ timeout: true }); }, limit('python')); }
        else if (data.type === 'out') stdout += data.text;
        else if (data.type === 'done') finish({ stderr: data.stderr, runtimeError: data.error ? explainTrap(data.error) : null });
      };
      const onErr = e => { e.preventDefault(); kill(); finish({ runtimeError: 'Python 运行环境加载失败，请检查网络' }); };
      w.addEventListener('message', onMsg);
      w.addEventListener('error', onErr);
      w.postMessage({ type: 'run', driver: J.PY_DRIVER, code, sig, input: J.encodeInput(cases) });
    });
  }

  function runJavascript(code, cases, phase) {
    phase('运行中…');
    const langUrl = new URL(A.lang, location.href).href;
    return runInWorker(A.jsWorker, { lang: langUrl, code, sig, input: J.encodeInput(cases) }, limit('javascript'));
  }

  const RUNNERS = { cpp: runCpp, python: runPython, javascript: runJavascript };

  // 开始写代码时就在后台准备编译器 / 解释器，点运行时少等一会儿
  function preload() {
    if (lang === 'cpp' && !cppW) { setStatus('准备 C++ 编译器…'); cppWorker().postMessage({ type: 'preload', pkg: CLANG_PKG }); }
    if (lang === 'python' && !pyW) { setStatus('加载 Python…'); pyWorker().postMessage({ type: 'preload' }); }
    if (lang === 'javascript') setStatus('');
  }

  /* ---------- 测试用例 ---------- */
  const casesEl = $('[data-panel="cases"]');
  const defaults = () => P.examples.map(e => e.in.map(v => JSON.stringify(v)));
  let cases = store.get(KEY_CASES, null);
  if (!Array.isArray(cases) || !cases.length || cases.some(c => !Array.isArray(c) || c.length !== sig.params.length)) cases = defaults();
  let curCase = 0;

  function renderCases() {
    curCase = Math.min(curCase, cases.length - 1);
    const tabs = cases.map((c, i) => `<span class="case-tab${i === curCase ? ' is-on' : ''}" data-case="${i}">Case ${i + 1}${cases.length > 1 ? `<button type="button" class="case-del" data-del="${i}" title="删除这个用例" aria-label="删除用例 ${i + 1}">×</button>` : ''}</span>`).join('');
    const fields = sig.params.map(([name, type], j) => `
      <label class="case-field">
        <span class="case-name">${esc(name)} <span class="c-muted">: ${esc(type)}</span></span>
        <textarea rows="1" spellcheck="false" autocomplete="off" data-arg="${j}">${esc(cases[curCase][j])}</textarea>
      </label>`).join('');
    casesEl.innerHTML = `
      <div class="case-tabs">${tabs}${cases.length < 8 ? '<button type="button" class="case-add" data-add title="复制当前用例，新增一个">+</button>' : ''}</div>
      <div class="case-fields">${fields}</div>
      <div class="case-foot c-muted">参数用 JSON 写，链表和二叉树写成数组（二叉树按层序，空节点写 null）。<button type="button" class="link-btn" data-cases-reset>恢复示例用例</button></div>`;
    $$('textarea', casesEl).forEach(autosize);
  }
  function autosize(t) { t.style.height = 'auto'; t.style.height = Math.min(t.scrollHeight, 160) + 'px'; }
  const saveCases = () => {
    if (JSON.stringify(cases) === JSON.stringify(defaults())) store.del(KEY_CASES);
    else store.set(KEY_CASES, cases);
  };
  casesEl.addEventListener('input', e => {
    const t = e.target.closest('[data-arg]');
    if (!t) return;
    cases[curCase][+t.dataset.arg] = t.value;
    autosize(t);
    saveCases();
  });
  casesEl.addEventListener('click', e => {
    const del = e.target.closest('[data-del]');
    if (del) {
      cases.splice(+del.dataset.del, 1);
      if (curCase >= +del.dataset.del && curCase > 0) curCase--;
      saveCases(); renderCases(); return;
    }
    const tab = e.target.closest('[data-case]');
    if (tab) { curCase = +tab.dataset.case; renderCases(); return; }
    if (e.target.closest('[data-add]')) { cases.push(cases[curCase].slice()); curCase = cases.length - 1; saveCases(); renderCases(); return; }
    if (e.target.closest('[data-cases-reset]')) { cases = defaults(); curCase = 0; saveCases(); renderCases(); }
  });
  renderCases();

  // 自定义用例：输入和某个已知用例相同时带上预期结果，否则只显示输出
  const known = new Map(P.examples.concat(P.tests).map(c => [JSON.stringify(c.in), c.out]));
  function customCases() {
    return cases.map((c, i) => {
      const args = c.map((s, j) => {
        try { return JSON.parse(s); } catch (e) { throw new Error(`Case ${i + 1} 的参数 ${sig.params[j][0]} 不是合法的 JSON：${s}`); }
      });
      const key = JSON.stringify(args);
      return known.has(key) ? { in: args, out: known.get(key) } : { in: args };
    });
  }

  /* ---------- 控制台：标签切换 ---------- */
  function showTab(name) {
    $$('[data-tab]').forEach(b => b.setAttribute('aria-selected', String(b.dataset.tab === name)));
    $$('[data-panel]').forEach(p => { p.hidden = p.dataset.panel !== name; });
  }
  $$('[data-tab]').forEach(b => b.addEventListener('click', () => showTab(b.dataset.tab)));

  /* ---------- 运行 / 提交 ---------- */
  const resultEl = $('[data-panel="result"]');
  const runBtn = $('[data-run]'), submitBtn = $('[data-submit]');
  runBtn.title = `运行示例用例（${MOD}+Enter）`;
  submitBtn.title = `用全部用例判题（${MOD}+Shift+Enter）`;
  $('[data-hint]').textContent = `${MOD}+Enter 运行 · ${MOD}+Shift+Enter 提交`;
  let busy = false;

  const STATUS = {
    AC: ['通过', 'ok'], WA: ['解答错误', 'bad'], RE: ['执行出错', 'bad'], TLE: ['超出时间限制', 'bad'],
    CE: ['编译出错', 'bad'], DONE: ['运行完成', 'ok'], SKIP: ['未运行', 'skip']
  };
  const fmtMs = ms => (ms < 0.01 ? '< 0.01' : ms < 1 ? ms.toFixed(2) : ms < 100 ? ms.toFixed(1) : Math.round(ms)) + ' ms';
  const show = v => JSON.stringify(v);

  function block(label, body, cls) {
    return `<div class="res-block${cls ? ' ' + cls : ''}"><div class="res-label">${label}</div><pre>${body}</pre></div>`;
  }
  function caseDetail(c, r) {
    let html = block('输入', sig.params.map((p, i) => `<span class="c-muted">${esc(p[0])} =</span> ${esc(show(c.in[i]))}`).join('\n'));
    if (r.stdout) html += block('标准输出', esc(r.stdout.replace(/\n$/, '')));
    if (r.status === 'SKIP') html += block('输出', '<span class="c-muted">前面的用例出错，没有运行</span>');
    else if (r.error) html += block(r.status === 'TLE' ? '超时' : '错误', esc(r.error), 'is-bad');
    else html += block('输出', esc(show(r.got)), r.status === 'WA' ? 'is-bad' : '');
    if ('out' in c) html += block('预期结果', esc(show(c.out)));
    return html;
  }

  function renderResult(mode, list, res) {
    const [name, cls] = STATUS[res.status];
    const times = res.cases.map(c => c.ms).filter(x => typeof x === 'number');
    const passed = res.cases.filter(c => c.status === 'AC').length;
    let head = `<div class="res-head is-${cls}"><b>${name}</b>`;
    if (mode === 'submit' && res.status !== 'CE') head += `<span>${passed} / ${list.length} 个用例通过</span>`;
    if (times.length && res.status !== 'CE') head += `<span class="c-muted">最长用时 ${fmtMs(Math.max(...times))}</span>`;
    head += `<span class="c-muted res-lang">${J.LANGS[lang].name}</span></div>`;

    if (res.status === 'CE') {
      resultEl.innerHTML = head + `<pre class="res-err">${esc(res.compileError)}</pre>`;
      return;
    }
    if (mode === 'submit') {
      const i = res.cases.findIndex(c => c.status !== 'AC');
      resultEl.innerHTML = head + (i < 0
        ? `<p class="res-note">全部用例通过，已记录到本机的做题记录里。</p>`
        : `<p class="res-note c-muted">第 ${i + 1} 个用例没有通过：</p>${caseDetail(list[i], res.cases[i])}`);
      return;
    }
    let cur = res.cases.findIndex(c => c.status !== 'AC' && c.status !== 'DONE' && c.status !== 'SKIP');
    if (cur < 0) cur = 0;
    const draw = () => {
      const tabs = res.cases.map((c, i) => `<button type="button" class="case-tab is-${STATUS[c.status][1]}${i === cur ? ' is-on' : ''}" data-rcase="${i}">${c.status === 'AC' ? '✓' : c.status === 'DONE' ? '•' : c.status === 'SKIP' ? '–' : '✗'} Case ${i + 1}</button>`).join('');
      resultEl.innerHTML = head + `<div class="case-tabs">${tabs}</div>` + caseDetail(list[cur], res.cases[cur]);
    };
    resultEl.onclick = e => { const b = e.target.closest('[data-rcase]'); if (b) { cur = +b.dataset.rcase; draw(); } };
    draw();
  }

  function mark(diags) {
    if (!monaco || !diags || !diags.length) return;
    const m = models[lang];
    monaco.editor.setModelMarkers(m, 'judge', diags.filter(d => d.line > 0 && d.line <= m.getLineCount()).map(d => {
      const col = Math.max(1, d.col || 1);
      const word = d.col ? m.getWordAtPosition({ lineNumber: d.line, column: col }) : null;
      return {
        startLineNumber: d.line, startColumn: word ? word.startColumn : (d.col ? col : m.getLineFirstNonWhitespaceColumn(d.line) || 1),
        endLineNumber: d.line, endColumn: word ? word.endColumn : m.getLineMaxColumn(d.line),
        message: d.msg,
        severity: d.severity === 'warning' ? monaco.MarkerSeverity.Warning : monaco.MarkerSeverity.Error
      };
    }));
  }

  function record(ac) {
    const s = store.get(KEY_STATE, {});
    const cur = s[P.slug];
    if (ac) s[P.slug] = { status: 'ac', at: Date.now(), lang };
    else if (!cur || cur.status !== 'ac') s[P.slug] = { status: 'tried', at: Date.now(), lang };
    store.set(KEY_STATE, s);
    showSolved();
  }
  function showSolved() {
    const s = store.get(KEY_STATE, {})[P.slug];
    $('[data-solved]').hidden = !(s && s.status === 'ac');
  }
  showSolved();

  async function execute(mode) {
    if (busy) return;
    let list;
    try {
      list = mode === 'run' ? customCases() : P.examples.concat(P.tests);
    } catch (e) {
      showTab('result');
      resultEl.innerHTML = `<div class="res-head is-bad"><b>用例格式有误</b></div><pre class="res-err">${esc(e.message)}</pre>`;
      return;
    }
    busy = true;
    runBtn.disabled = submitBtn.disabled = true;
    root.classList.add('is-busy');
    showTab('result');
    const label = mode === 'run' ? '运行' : '提交';
    const phase = t => { resultEl.innerHTML = `<div class="res-pending"><span class="spinner" aria-hidden="true"></span>${label}：${esc(t)}</div>`; };
    if (monaco) monaco.editor.setModelMarkers(models[lang], 'judge', []);
    const code = getCode();
    store.set(codeKey(lang), code);
    try {
      const run = await RUNNERS[lang](code, list, phase);
      const res = J.evaluate(sig, list, run);
      mark((res.diagnostics || []).concat(run.diagnostics || []));
      const bad = res.cases.find(c => c.status === 'RE' && c.line);
      if (bad) mark([{ line: bad.line, msg: bad.error, severity: 'error' }]);
      renderResult(mode, list, res);
      if (mode === 'submit') record(res.status === 'AC');
    } catch (e) {
      resultEl.innerHTML = `<div class="res-head is-bad"><b>出错了</b></div><pre class="res-err">${esc(e && e.stack || e)}</pre>`;
    }
    busy = false;
    runBtn.disabled = submitBtn.disabled = false;
    root.classList.remove('is-busy');
  }

  runBtn.addEventListener('click', () => execute('run'));
  submitBtn.addEventListener('click', () => execute('submit'));
  document.addEventListener('keydown', e => {
    if (!(e.metaKey || e.ctrlKey) || e.key !== 'Enter') return;
    e.preventDefault();
    execute(e.shiftKey ? 'submit' : 'run');
  });

  /* ---------- 分栏拖动 ---------- */
  const layout = store.get(KEY_LAYOUT, {});
  const applyLayout = () => {
    if (layout.x) root.style.setProperty('--ide-left', layout.x + '%');
    if (layout.y) root.style.setProperty('--ide-console', layout.y + 'px');
  };
  applyLayout();
  $$('[data-gutter]').forEach(g => {
    g.addEventListener('pointerdown', e => {
      e.preventDefault();
      g.setPointerCapture(e.pointerId);
      root.classList.add('is-dragging');
      const rect = root.getBoundingClientRect();
      const right = $('.ide-right', root).getBoundingClientRect();
      const move = ev => {
        if (g.dataset.gutter === 'x') layout.x = Math.max(25, Math.min(70, (ev.clientX - rect.left) / rect.width * 100));
        else layout.y = Math.max(120, Math.min(right.height - 160, right.bottom - ev.clientY));
        applyLayout();
      };
      const up = () => {
        g.removeEventListener('pointermove', move);
        g.removeEventListener('pointerup', up);
        root.classList.remove('is-dragging');
        store.set(KEY_LAYOUT, layout);
      };
      g.addEventListener('pointermove', move);
      g.addEventListener('pointerup', up);
    });
  });

  initEditor();
})();
