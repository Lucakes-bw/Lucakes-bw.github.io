/**
 * 「照着敲」：直接在文章的代码块上敲。没敲的代码是灰的，敲过的恢复语法高亮。
 *
 * 规则（和打字练习软件一样）：
 *   - 敲错了不前进：当前字符标红，要敲对才能继续，敲错的次数算进正确率
 *   - 自动跳过：行首缩进、注释、非 ASCII 字符（字符串里的中文、全角标点）、行尾空白；
 *     连续多个空格（对齐用的）只需要敲一个；每行末尾敲回车，下一行的缩进自动跳过
 *   - 进度存在本机，刷新后接着敲
 *
 * LTTyper.create({ fig, pre, onProgress(stats), onDone(stats), load(), save(state), shortcut(e) })
 *   → { start(), stop(), reset(), focus(), stats(), active }
 * stats：{ pos, total, keys, errors, ms, done }
 */
(function () {
  'use strict';

  const IDLE = 5000;            // 两次按键间隔超过 5 秒不算进用时（中间去干别的了）

  // 每一行哪些字符要敲：返回和行文本等长的数组，true 表示要敲；inBlock 是跨行的 /* */ 状态
  function typable(text, state) {
    const flags = new Array(text.length).fill(false);
    let i = 0;
    if (state.inBlock) {
      const end = text.indexOf('*/');
      if (end < 0) return flags;
      state.inBlock = false;
      i = end + 2;
    }
    // 找到代码部分的结尾：行尾的 // 或 /* 注释不敲
    let q = null, codeEnd = text.length;
    for (let k = i; k < text.length; k++) {
      const c = text[k];
      if (q) { if (c === '\\') k++; else if (c === q) q = null; continue; }
      if (c === '"' || c === "'") q = c;
      else if (c === '/' && text[k + 1] === '/') { codeEnd = k; break; }
      else if (c === '/' && text[k + 1] === '*') {
        const end = text.indexOf('*/', k + 2);
        if (end < 0) { state.inBlock = true; codeEnd = k; break; }
        if (!text.slice(end + 2).trim()) { codeEnd = k; break; }
      }
    }
    while (codeEnd > i && /\s/.test(text[codeEnd - 1])) codeEnd--;
    while (i < codeEnd && /\s/.test(text[i])) i++;
    for (let k = i; k < codeEnd; k++) {
      const c = text[k];
      if (c.charCodeAt(0) > 126) continue;                       // 中文、全角标点：跳过
      if (/\s/.test(c) && /\s/.test(text[k - 1] || '') && k > i) continue;   // 连续空白只敲第一个
      flags[k] = true;
    }
    return flags;
  }

  // 把一行里的文字拆成一个字符一个 span（保留 highlight.js 的语法高亮 span）
  function wrapChars(line) {
    const out = [];
    const walk = node => {
      Array.from(node.childNodes).forEach(n => {
        if (n.nodeType === 3) {
          const frag = document.createDocumentFragment();
          for (const ch of n.data) {
            const s = document.createElement('span');
            s.className = 'tc';
            s.textContent = ch;
            frag.appendChild(s);
            out.push(s);
          }
          n.replaceWith(frag);
        } else if (n.nodeType === 1) walk(n);
      });
    };
    walk(line);
    return out;
  }

  function create(opt) {
    const { fig, pre } = opt;
    let lines = [], saved = [], spans = [], tokens = [], input = null;
    let pos = 0, keys = 0, errors = 0, ms = 0, last = 0, cur = -1, active = false, flash = null;

    const stats = () => ({ pos, total: tokens.length, keys, errors, ms, done: pos >= tokens.length });

    function build() {
      lines = Array.from(pre.querySelectorAll('.line'));
      saved = lines.map(l => l.innerHTML);
      spans = [];
      tokens = [];
      const state = { inBlock: false };
      lines.forEach(line => {
        const text = line.textContent;
        const flags = typable(text, state);
        // highlight.js 偶尔会把一个字符拆到不同的 span 里，按 Array.from 的码点数对齐
        const chars = wrapChars(line);
        let lastTok = null;
        let k = 0;
        chars.forEach(s => {
          spans.push(s);
          if (flags[k]) {
            const ch = /\s/.test(s.textContent) ? ' ' : s.textContent;
            tokens.push({ el: s, idx: spans.length - 1, ch });
            lastTok = s;
          }
          k += s.textContent.length;
        });
        if (lastTok) {
          // 回车的位置紧跟在这一行最后一个要敲的字符后面（行尾注释之前），光标不会跳到注释末尾。
          // 在 spans 里它排在整行之后：光标停在这里时，行尾注释已经显示成「敲过」
          const nl = document.createElement('span');
          nl.className = 'tc tc-nl';
          lastTok.after(nl);
          spans.push(nl);
          tokens.push({ el: nl, idx: spans.length - 1, ch: '\n' });
        }
      });
      // 最后一行敲完就结束，不用再敲回车
      if (tokens.length && tokens[tokens.length - 1].ch === '\n') tokens.pop();
    }

    // 光标之前的都恢复高亮，之后的变灰
    function render() {
      const idx = pos < tokens.length ? tokens[pos].idx : spans.length;
      const from = Math.min(cur < 0 ? 0 : cur, idx), to = Math.max(cur < 0 ? spans.length : cur, idx);
      for (let i = cur < 0 ? 0 : from; i < (cur < 0 ? spans.length : to); i++) spans[i].classList.toggle('typed', i < idx);
      if (cur >= 0 && spans[cur]) spans[cur].classList.remove('cur', 'bad');
      cur = idx;
      if (spans[cur] && active) spans[cur].classList.add('cur');
      fig.classList.toggle('tc-done', pos >= tokens.length);
      placeInput();
    }

    // 隐藏的输入框跟着光标走：手机键盘、输入法候选框出现在光标附近
    function placeInput() {
      if (!input) return;
      const el = spans[cur] || spans[spans.length - 1];
      if (!el) return;
      const r = el.getBoundingClientRect(), f = fig.getBoundingClientRect();
      input.style.left = Math.max(0, r.left - f.left) + 'px';
      input.style.top = Math.max(0, r.top - f.top) + 'px';
    }
    function reveal() {
      const el = spans[cur];
      if (!el) return;
      // 横向：光标在第一屏宽度以内就回到最左边（行号一直看得见），超出时让光标停在中间。
      // 纵向：光标快出屏幕时才滚
      const box = pre.closest('.code-scroll') || pre.parentElement;
      const r = el.getBoundingClientRect(), b = box.getBoundingClientRect();
      const off = r.left - b.left + box.scrollLeft;
      const target = off < box.clientWidth - 40 ? 0 : Math.round(off - box.clientWidth / 2);
      if (r.right > b.right - 24 || r.left < b.left + 8 || (target === 0 && box.scrollLeft)) box.scrollLeft = target;
      if (r.bottom > window.innerHeight - 80 || r.top < 60) el.scrollIntoView({ block: 'center', behavior: 'smooth' });
    }

    function type(ch) {
      if (pos >= tokens.length) return;
      const now = Date.now();
      if (last) ms += Math.min(now - last, IDLE);
      last = now;
      keys++;
      const exp = tokens[pos];
      if (ch === exp.ch || (exp.ch === ' ' && ch === '\t')) {
        pos++;
        render();
        reveal();
        opt.save({ pos, keys, errors, ms });
        if (opt.onProgress) opt.onProgress(stats());
        if (pos >= tokens.length && opt.onDone) opt.onDone(stats());
      } else {
        errors++;
        const el = spans[cur];
        el.classList.remove('bad');
        void el.offsetWidth;                     // 重新触发闪红动画
        el.classList.add('bad');
        clearTimeout(flash);
        flash = setTimeout(() => el.classList.remove('bad'), 600);
        opt.save({ pos, keys, errors, ms });
        if (opt.onProgress) opt.onProgress(stats(), exp.ch === '\n' ? '这里该换行了（回车）' : `这里应该是 ${exp.ch === ' ' ? '空格' : exp.ch}`);
      }
    }

    function onKey(e) {
      if (e.metaKey || e.ctrlKey || e.altKey) { if (opt.shortcut) opt.shortcut(e); return; }
      if (e.isComposing) return;
      if (e.key === 'Backspace') {
        e.preventDefault();
        if (pos > 0) { pos--; render(); reveal(); opt.save({ pos, keys, errors, ms }); if (opt.onProgress) opt.onProgress(stats()); }
      } else if (e.key === 'Enter') {
        e.preventDefault();
        type('\n');
      } else if (e.key === 'Tab') {
        e.preventDefault();                       // 缩进是自动跳过的，Tab 不算字符
      } else if (e.key === 'Escape') {
        input.blur();
      }
    }
    function onInput() {
      if (input.value === '' || input.composing) return;
      const text = input.value;
      input.value = '';
      for (const ch of text) type(ch === '\n' ? '\n' : ch);
    }

    function start() {
      if (active) return;
      active = true;
      build();
      const st = opt.load() || {};
      pos = Math.min(Math.max(0, st.pos | 0), tokens.length);
      keys = st.keys | 0; errors = st.errors | 0; ms = st.ms | 0; last = 0; cur = -1;
      fig.classList.add('is-typing');
      input = document.createElement('textarea');
      input.className = 'tc-input';
      input.setAttribute('autocapitalize', 'off');
      input.setAttribute('autocomplete', 'off');
      input.setAttribute('autocorrect', 'off');
      input.spellcheck = false;
      input.setAttribute('aria-label', '照着敲：在这里输入');
      fig.appendChild(input);
      input.addEventListener('keydown', onKey);
      input.addEventListener('input', onInput);
      input.addEventListener('compositionstart', () => { input.composing = true; });
      input.addEventListener('compositionend', () => { input.composing = false; onInput(); });
      input.addEventListener('focus', () => { fig.classList.remove('tc-blur'); last = 0; });
      input.addEventListener('blur', () => fig.classList.add('tc-blur'));
      pre.addEventListener('mousedown', focusOnClick);
      render();
      fig.classList.add('tc-blur');
      if (opt.onProgress) opt.onProgress(stats());
    }
    function focusOnClick(e) { e.preventDefault(); focus(); }
    function focus() {
      if (!input) return;
      input.focus({ preventScroll: true });
      // 窗口本身不在前台时 focus 事件不会触发，按实际的焦点设置提示
      fig.classList.toggle('tc-blur', document.activeElement !== input);
      placeInput();
      reveal();
    }

    function stop() {
      if (!active) return;
      active = false;
      lines.forEach((l, i) => { l.innerHTML = saved[i]; });
      spans = []; tokens = []; cur = -1;
      pre.removeEventListener('mousedown', focusOnClick);
      if (input) input.remove();
      input = null;
      fig.classList.remove('is-typing', 'tc-blur', 'tc-done');
    }

    function reset() {
      pos = keys = errors = ms = 0;
      last = 0;
      opt.save(null);
      render();
      focus();
      if (opt.onProgress) opt.onProgress(stats());
    }

    return { start, stop, reset, focus, stats, get active() { return active; } };
  }

  window.LTTyper = { create, _typable: typable };
})();
