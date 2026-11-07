/**
 * 做题页和文章代码块共用的浏览器端运行环境：
 *   loadMonaco / applyTheme / editorOptions   Monaco 编辑器（从 CDN 加载，配色跟随主题的 CSS 变量）
 *   compiler(pkg, hooks)                      常驻的 Clang 编译 Worker（见 cpp-worker.js）
 *   runWasm(url, wasm, stdin, ms)             在一次性 Worker 里运行编译好的 wasm（见 wasi-worker.js），超时直接 terminate
 *   explainTrap(msg)                          把 wasm 的 trap 信息翻译成中文提示
 */
(function () {
  'use strict';

  const MONACO = 'https://cdn.jsdelivr.net/npm/monaco-editor@0.52.2/min';

  let monacoP = null;
  function loadMonaco() {
    monacoP = monacoP || new Promise((resolve, reject) => {
      const s = document.createElement('script');
      s.src = MONACO + '/vs/loader.js';
      s.onerror = e => { monacoP = null; reject(e); };
      s.onload = () => {
        // 编辑器的 Worker 跨域加载：用同源的 blob 包一层
        window.MonacoEnvironment = {
          getWorkerUrl: () => URL.createObjectURL(new Blob(
            [`self.MonacoEnvironment={baseUrl:'${MONACO}/'};importScripts('${MONACO}/vs/base/worker/workerMain.js');`],
            { type: 'text/javascript' }))
        };
        window.require.config({ paths: { vs: MONACO + '/vs' }, 'vs/nls': { availableLanguages: { '*': 'zh-cn' } } });
        window.require(['vs/editor/editor.main'], () => resolve(window.monaco), e => { monacoP = null; reject(e); });
      };
      document.head.appendChild(s);
    });
    return monacoP;
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
  function defineTheme(monaco) {
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
        'editorSuggestWidget.selectedForeground': c['--fg'],
        'editorSuggestWidget.selectedIconForeground': c['--fg'],
        'editorSuggestWidget.foreground': c['--fg-2'],
        'editorSuggestWidget.highlightForeground': c['--accent'],
        'editorSuggestWidget.focusHighlightForeground': c['--accent'],
        'list.activeSelectionForeground': c['--fg'],
        'list.activeSelectionIconForeground': c['--fg'],
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
  // 应用主题，并在切换深浅色时重新生成（同一个页面只挂一次监听）
  let watching = false;
  function applyTheme(monaco) {
    defineTheme(monaco);
    if (watching) return;
    watching = true;
    new MutationObserver(() => defineTheme(monaco)).observe(document.documentElement, { attributes: true, attributeFilter: ['data-scheme'] });
  }

  const editorOptions = () => ({
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
    unicodeHighlight: { ambiguousCharacters: false },    // 中文注释和字符串里的全角标点不要加框
    fixedOverflowWidgets: true,
    scrollbar: { verticalScrollbarSize: 10, horizontalScrollbarSize: 10 }
  });

  /**
   * 常驻的编译 Worker。hooks：
   *   progress(done, total)  首次下载编译器的进度
   *   ready(ok)              preload 完成
   * 返回 { preload(), compile(args, files) → Promise<{ ok, wasm?, stderr }>, isReady() }
   */
  function compiler(workerUrl, pkg, hooks = {}) {
    let w = null, ready = false, seq = 0;
    const waiting = {};
    // 下载卡住的看门狗：网络请求有时会一直挂着既不完成也不报错。
    // 下载过程中 20 秒没有任何进度就放弃这个 Worker，正在等的编译都按「加载失败」返回，下次重新下载。
    // 另外，Worker 启动后 30 秒一条消息都没有（连不上 CDN，一个进度都收不到）也报错，不能一直停在「下载编译器…」
    const STALL = 20000, SILENT = 30000;
    let lastProgress = 0, downloading = false, watchdog = null, startedAt = 0, heard = false;
    function fail(msg) {
      if (w) w.terminate();
      w = null;
      ready = false;
      downloading = false;
      clearInterval(watchdog);
      watchdog = null;
      Object.keys(waiting).forEach(id => { waiting[id]({ ok: false, stderr: msg }); delete waiting[id]; });
    }
    function watch() {
      if (watchdog) return;
      watchdog = setInterval(() => {
        if (ready) { clearInterval(watchdog); watchdog = null; return; }
        if (downloading && Date.now() - lastProgress > STALL) fail('下载编译器卡住了（20 秒没有进展）');
        else if (!heard && Date.now() - startedAt > SILENT) fail('连不上编译器的下载地址（30 秒没有响应）');
      }, 2000);
    }
    function worker() {
      if (w) return w;
      w = new Worker(workerUrl, { type: 'module' });
      startedAt = Date.now();
      heard = false;
      watch();
      w.onmessage = ({ data }) => {
        heard = true;
        if (data.type === 'progress' && data.total) {
          lastProgress = Date.now();
          downloading = data.done < data.total;
          if (!ready && hooks.progress) hooks.progress(data.done, data.total);
        } else if (data.type === 'ready') {
          ready = data.ok;
          if (hooks.ready) hooks.ready(data.ok);
        } else if (data.type === 'compiled') {
          ready = true;
          const done = waiting[data.id];
          delete waiting[data.id];
          if (done) done(data);
        }
      };
      w.onerror = e => {
        e.preventDefault();
        fail('编译器加载失败：' + (e.message || '请检查网络'));
      };
      return w;
    }
    return {
      started: () => !!w,
      isReady: () => ready,
      preload() { if (!w) worker().postMessage({ type: 'preload', pkg }); },
      compile(args, files) {
        return new Promise(resolve => {
          const id = ++seq;
          waiting[id] = resolve;
          worker().postMessage({ type: 'compile', id, pkg, args, files });
        }).then(r => {
          // 失败了却没有编译器的诊断信息：多半是编译器没下载完（断网、CDN 出错）。
          // Worker 里会留着失败的状态，扔掉重建，下次运行重新下载
          if (!r.ok && !/error:/.test(r.stderr || '')) {
            if (w) { w.terminate(); w = null; }
            ready = false;
            downloading = false;
            r.stderr = '编译器加载失败，请检查网络后重试。\n' + (r.stderr || '');
            r.loadError = true;
          }
          return r;
        });
      }
    };
  }

  /**
   * 跑一次一次性的 Worker：超时直接 terminate；标准输出边跑边收（onOut 可以实时显示）
   * 结果 { stdout, stderr, exitCode, error?, timeout? }
   */
  function runInWorker(url, msg, ms, opts, onOut) {
    return new Promise(resolve => {
      const w = new Worker(url, opts);
      let stdout = '';
      const finish = r => { clearTimeout(timer); w.terminate(); resolve(Object.assign({ stdout, stderr: '', exitCode: null }, r)); };
      const timer = setTimeout(() => finish({ timeout: true }), ms);
      w.onmessage = ({ data }) => {
        if (data.type === 'out') { stdout += data.text; if (onOut) onOut(data.text); }
        else if (data.type === 'done') {
          finish({ stderr: data.stderr || '', exitCode: data.exitCode, error: data.error ? explainTrap(data.error) : null });
        }
      };
      w.onerror = e => { e.preventDefault(); finish({ error: e.message || '运行失败' }); };
      w.postMessage(msg);
    });
  }
  const runWasm = (url, wasm, stdin, ms, onOut) => runInWorker(url, { wasm, stdin: stdin || '' }, ms, undefined, onOut);

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

  // 去掉 clang 输出里的颜色控制符
  const cleanClang = text => (text || '').replace(/\x1b\[[0-9;]*m/g, '').trim();

  window.LTIde = { MONACO, loadMonaco, applyTheme, editorOptions, compiler, runInWorker, runWasm, explainTrap, cleanClang };
})();
