/**
 * 文章代码块「跟练」的判定和编译参数：浏览器（snippet.js）和 Node（tools/check-snippets.mjs）共用
 *
 * classify(lang, code, opt) 判断一段代码能不能在浏览器里直接运行：
 *   - 语言是 c / cpp，有 main 函数
 *   - 只包含白名单里的标准头文件（没有 fork / socket / 线程 / 文件系统 / 第三方库）
 *   - C++ 不能用异常（浏览器里的 libc++ 编译时关掉了异常）
 * 返回 { runnable, reason?, stdin }，reason 说明为什么不能运行。
 *
 * 作者可以在代码块前一行用注释覆盖自动判定：
 *   <!-- run: off -->                   不显示运行按钮
 *   <!-- run: on -->                    跳过检查，强制显示运行按钮
 *   <!-- run: stdin="3 4\n5" -->        运行时的标准输入（\n 表示换行）
 *   <!-- run: flags="-O0" -->           额外的编译参数
 *   <!-- run: error -->                 这段代码本来就是演示错误的（编译不过或运行出错），照样能运行，
 *                                       检查脚本反过来要求它出错；这种代码不提供跟练
 *   <!-- run: blank="5,8-9" -->         「填空」挖掉哪几行（代码块里的行号，从 1 开始），不写就自动挑
 *
 * 跟练（第二期）用到的：
 *   blanks(code, spec)        填空要挖掉的行
 *   typingLines(code)         照着敲要比对的行（去掉注释和空行）；matchPrefix 比对一行敲到了哪里
 *   unstable(code)            输出每次运行都不一样（时间、随机数）：判定时只要求能正常运行
 *   compareOutput(got, exp)   比较输出：忽略行尾空白、末尾空行，地址 0x… 不比
 *   extractBlocks(md)         从 Markdown 里取出代码块和它前面的 <!-- run --> 注释（构建和检查脚本用）
 * 多个选项用空格隔开，比如 <!-- run: on stdin="1 2" -->。
 */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.LTSnippet = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  const CPP_HEADERS = ['algorithm', 'any', 'array', 'atomic', 'bit', 'bitset', 'cassert', 'cctype', 'cerrno', 'cfloat',
    'charconv', 'chrono', 'cinttypes', 'climits', 'cmath', 'compare', 'complex', 'concepts', 'cstdarg', 'cstddef',
    'cstdint', 'cstdio', 'cstdlib', 'cstring', 'ctime', 'cwchar', 'cwctype', 'deque', 'expected', 'format', 'forward_list',
    'functional', 'initializer_list', 'iomanip', 'ios', 'iosfwd', 'iostream', 'istream', 'iterator', 'limits', 'list',
    'map', 'memory', 'memory_resource', 'new', 'numbers', 'numeric', 'optional', 'ostream', 'print', 'queue', 'random',
    'ranges', 'ratio', 'scoped_allocator', 'set', 'source_location', 'span', 'sstream', 'stack', 'stdexcept',
    'streambuf', 'string', 'string_view', 'tuple', 'type_traits', 'typeindex', 'typeinfo', 'unordered_map',
    'unordered_set', 'utility', 'valarray', 'variant', 'vector', 'version'];
  const C_HEADERS = ['assert.h', 'complex.h', 'ctype.h', 'errno.h', 'fenv.h', 'float.h', 'inttypes.h', 'iso646.h',
    'limits.h', 'math.h', 'stdalign.h', 'stdarg.h', 'stdbool.h', 'stddef.h', 'stdint.h', 'stdio.h', 'stdlib.h',
    'stdnoreturn.h', 'string.h', 'strings.h', 'tgmath.h', 'time.h', 'uchar.h', 'wchar.h', 'wctype.h'];
  const ALLOWED = new Set(CPP_HEADERS.concat(C_HEADERS));

  // 头文件 → 不能运行的原因（只用来给出更具体的提示）
  const WHY = [
    [/^(unistd|fcntl|signal|setjmp|dirent|dlfcn|poll|termios|pwd|grp|syslog)\.h$|^sys\//, '用到了 Linux 系统调用'],
    [/^(arpa|netinet|netdb)\/|^sys\/(socket|epoll|select)\.h$/, '用到了网络 socket'],
    [/^(thread|mutex|shared_mutex|condition_variable|future|semaphore|latch|barrier|stop_token|pthread\.h)$/, '用到了多线程'],
    [/^(fstream|filesystem|cstdio_ext)$/, '用到了文件系统'],
    [/^lib(av|sw)|^SDL/, '依赖 FFmpeg / SDL'],
    [/^Q[A-Z]|^Qt/, '依赖 Qt'],
    [/^(mysql|hiredis|sqlite3|openssl|curl|boost|gtest)/i, '依赖第三方库']
  ];

  const LANG_ALIAS = { c: 'c', h: 'c', cpp: 'cpp', 'c++': 'cpp', cc: 'cpp', cxx: 'cpp', hpp: 'cpp' };
  const normLang = l => LANG_ALIAS[String(l || '').toLowerCase()] || null;

  // 去掉注释和字符串，避免把注释里的 throw / main 当真
  function strip(code) {
    return code
      .replace(/\/\*[\s\S]*?\*\//g, ' ')
      .replace(/\/\/.*$/gm, '')
      .replace(/R"([^(\s]*)\([\s\S]*?\)\1"/g, '""')
      .replace(/"(?:\\.|[^"\\\n])*"/g, '""')
      .replace(/'(?:\\.|[^'\\\n])*'/g, "''");
  }

  function includes(code) {
    const out = [];
    const re = /^\s*#\s*include\s*([<"])([^>"]+)[>"]/gm;
    let m;
    while ((m = re.exec(code))) out.push({ name: m[2].trim(), local: m[1] === '"' });
    return out;
  }

  const readsStdin = code => /\b(cin|scanf|getchar|fgets|getline|gets|getc|fread|fscanf\s*\(\s*stdin)\b/.test(strip(code));

  // 解析 <!-- run: ... --> 里的选项
  function parseOptions(text) {
    const opt = {};
    if (!text) return opt;
    const m = /^\s*run\b:?\s*([\s\S]*?)\s*$/.exec(text);
    if (!m) return opt;
    const re = /(\w+)(?:="((?:\\.|[^"\\])*)")?/g;
    let t;
    while ((t = re.exec(m[1]))) {
      const [, key, val] = t;
      if (key === 'on' || key === 'off') opt.mode = key;
      else if (key === 'error') opt.error = true;
      else if (val !== undefined) opt[key] = val.replace(/\\(.)/g, (_, c) => (c === 'n' ? '\n' : c === 't' ? '\t' : c));
    }
    return opt;
  }

  function classify(lang, code, opt = {}) {
    lang = normLang(lang);
    const res = { lang, runnable: false, error: !!opt.error, stdin: opt.stdin || '', flags: opt.flags ? opt.flags.split(/\s+/).filter(Boolean) : [] };
    res.readsStdin = readsStdin(code);
    if (!lang) return Object.assign(res, { reason: '不是 C/C++ 代码' });
    if (opt.mode === 'off') return Object.assign(res, { reason: '作者关闭了运行' });
    if (opt.mode === 'on') return Object.assign(res, { runnable: true });
    const body = strip(code);
    if (!/\bmain\s*\(/.test(body)) return Object.assign(res, { reason: '代码片段，没有 main 函数' });
    const incs = includes(code);
    if (!incs.length) return Object.assign(res, { reason: '代码片段，省略了头文件' });
    for (const inc of incs) {
      if (inc.local) return Object.assign(res, { reason: `用到了其他文件 "${inc.name}"` });
      if (!ALLOWED.has(inc.name)) {
        const w = WHY.find(x => x[0].test(inc.name));
        return Object.assign(res, { reason: `${w ? w[1] : '用到了浏览器里没有的头文件'}（<${inc.name}>）` });
      }
    }
    if (lang === 'cpp' && /\b(throw|try\s*\{|catch\s*\()/.test(body)) return Object.assign(res, { reason: '用到了异常（浏览器里的 C++ 不支持异常）' });
    if (/(?<![.\w>:])(fopen|freopen|remove|rename|tmpfile|system|popen)\s*\(/.test(body)) return Object.assign(res, { reason: '用到了文件或进程操作' });
    return Object.assign(res, { runnable: true });
  }

  const FILE = { c: 'main.c', cpp: 'main.cpp' };
  const LINK = ['-Wl,-z,stack-size=8388608', '-Wl,--max-memory=536870912'];
  function compileArgs(lang, flags = []) {
    if (lang === 'c') return ['clang', '-std=gnu17', '-O2', ...LINK, ...flags, 'main.c', '-lm', '-o', 'main.wasm'];
    return ['clang++', '-std=c++20', '-O2', '-fno-exceptions', ...LINK, ...flags, 'main.cpp', '-o', 'main.wasm'];
  }
  const compileFiles = (lang, code) => ({ [FILE[lang]]: code });

  // clang 诊断 → [{ line, col, severity, msg }]
  function diagnostics(lang, text) {
    const out = [];
    const re = new RegExp(`^${FILE[lang].replace('.', '\\.')}:(\\d+):(\\d+): (error|warning): (.*)$`, 'gm');
    let m;
    while ((m = re.exec(text || ''))) out.push({ line: +m[1], col: +m[2], severity: m[3], msg: m[4] });
    return out;
  }

  // 代码内容的短哈希：草稿按它保存，文章里的代码改了以后旧草稿自动失效
  function hash(s) {
    let h = 0x811c9dc5;
    for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 0x01000193); }
    return (h >>> 0).toString(36);
  }


  /* ---------- 跟练 ---------- */

  // 一行代码拆成代码和行尾的 // 注释（跳过字符串和字符字面量里的 //）
  function splitComment(line) {
    let q = null;
    for (let i = 0; i < line.length; i++) {
      const c = line[i];
      if (q) { if (c === '\\') i++; else if (c === q) q = null; continue; }
      if (c === '"' || c === "'") q = c;
      else if (c === '/' && line[i + 1] === '/') return { code: line.slice(0, i).replace(/\s+$/, ''), comment: line.slice(i) };
      else if (c === '/' && line[i + 1] === '*') {
        const end = line.indexOf('*/', i + 2);
        if (end < 0) return { code: line.slice(0, i).replace(/\s+$/, ''), comment: line.slice(i) };
        if (!line.slice(end + 2).trim()) return { code: line.slice(0, i).replace(/\s+$/, ''), comment: line.slice(i) };
      }
    }
    return { code: line.replace(/\s+$/, ''), comment: '' };
  }

  // 比较用的规范形式：去掉注释，空白只在两个标识符之间才算数
  const normLine = line => splitComment(line).code.trim().replace(/\s+/g, ' ').replace(/ ?([^\w ]) ?/g, '$1');

  // 照着敲：需要敲的行（注释行、空行不算）。raw 是去掉行尾注释后的原文，用来显示提示
  function typingLines(code) {
    const out = [];
    let inBlock = false;
    code.split('\n').forEach((line, i) => {
      const t = line.trim();
      if (inBlock) { if (t.includes('*/')) inBlock = false; return; }
      if (t.startsWith('/*') && !t.includes('*/')) { inBlock = true; return; }
      const n = normLine(line);
      if (n) out.push({ line: i, raw: splitComment(line).code.trim(), norm: n });
    });
    return out;
  }

  // typed 是不是 expected 的前缀（空白宽松比较）。是的话返回 expected 里对应到的位置，不是返回 -1
  function matchPrefix(expected, typed) {
    const e = expected, t = typed.trim();
    let i = 0, j = 0;
    while (j < t.length) {
      if (/\s/.test(t[j])) { j++; continue; }
      while (i < e.length && /\s/.test(e[i])) i++;
      if (i >= e.length || e[i] !== t[j]) return -1;
      i++; j++;
    }
    return i;
  }

  // 「填空」：适合挖掉的行——在函数体里、是一条完整的语句，不是括号、return 0、单纯的输出
  function blankCandidates(code) {
    const lines = code.split('\n');
    const out = [];
    let depth = 0;
    lines.forEach((line, i) => {
      const { code: c } = splitComment(line);
      const t = c.trim();
      const before = depth;
      for (const ch of strip(c)) { if (ch === '{') depth++; else if (ch === '}') depth--; }
      if (before < 1 || t.length < 6) return;
      if (/^(#|\/\/|\*|\}|\{|case\b|default:|public:|private:|protected:|else\s*\{?$|return\s+0;|break;|continue;)/.test(t)) return;
      if (!/[;{]$/.test(t)) return;                                   // 跨行的语句只挖一半没法填
      const p = strip(t);
      if ((p.match(/\(/g) || []).length !== (p.match(/\)/g) || []).length) return;
      if (/^(printf|puts|std::cout|cout|std::printf|putchar)\b/.test(t) && !/[+\-*/%]|\w\(\w/.test(p.replace(/^\w+(::\w+)?\s*(<<|\()/, ''))) return;  // 只打印常量的行没什么好练
      out.push(i);
    });
    return out;
  }
  // spec 是作者给的 "5,8-9"（从 1 开始）；没给时从候选行里均匀挑，最多 5 行
  function blanks(code, spec) {
    const n = code.split('\n').length;
    if (spec) {
      const out = [];
      String(spec).split(',').forEach(part => {
        const m = /^\s*(\d+)\s*(?:-\s*(\d+))?\s*$/.exec(part);
        if (!m) return;
        for (let k = +m[1]; k <= +(m[2] || m[1]); k++) if (k >= 1 && k <= n) out.push(k - 1);
      });
      return Array.from(new Set(out)).sort((a, b) => a - b);
    }
    const cand = blankCandidates(code);
    const want = Math.max(1, Math.min(5, Math.round(cand.length / 4)));
    if (cand.length <= want) return cand;
    const out = [];
    for (let k = 0; k < want; k++) out.push(cand[Math.floor((k + 0.5) * cand.length / want)]);
    return out;
  }
  const BLANK_MARK = '补全这一行';
  const CIRCLED = '①②③④⑤⑥⑦⑧⑨⑩';
  // 挖好空的代码：保留缩进和原来的行尾注释（当提示）
  function blankCode(code, lines) {
    const src = code.split('\n');
    lines.forEach((i, k) => {
      const indent = /^\s*/.exec(src[i])[0];
      const { comment } = splitComment(src[i]);
      src[i] = `${indent}/* ${CIRCLED[k] || k + 1} ${BLANK_MARK} */${comment ? '  ' + comment : ''}`;
    });
    return src.join('\n');
  }

  // 输出每次不一样的程序：用到了时间、随机数
  const unstable = code => /\b(time|clock|rand|srand|random_device|mt19937|steady_clock|system_clock|high_resolution_clock|getpid|now)\s*[(<:]/.test(strip(code));

  function normOutput(s) {
    return String(s || '').replace(/\r\n/g, '\n').replace(/0x[0-9a-fA-F]+/g, '0x…')
      .split('\n').map(l => l.replace(/\s+$/, '')).join('\n').replace(/\n+$/, '');
  }
  // → { ok, line?, got?, exp? }  line 是第一处不同的行号（从 1 开始）
  function compareOutput(got, exp) {
    const a = normOutput(got), b = normOutput(exp);
    if (a === b) return { ok: true };
    const x = a.split('\n'), y = b.split('\n');
    let i = 0;
    while (i < x.length && i < y.length && x[i] === y[i]) i++;
    return { ok: false, line: i + 1, got: i < x.length ? x[i] : '（没有这一行）', exp: i < y.length ? y[i] : '（应该到此结束）' };
  }

  // Markdown 里的代码块：{ lang, code, line（围栏所在行，从 1 开始）, opt }
  function extractBlocks(md) {
    const out = [];
    const lines = md.split('\n');
    for (let i = 0; i < lines.length; i++) {
      const m = /^(\s*)(`{3,}|~{3,})\s*([\w+#-]*)/.exec(lines[i]);
      if (!m) continue;
      const [, indent, fence, lang] = m;
      const start = i;
      const body = [];
      const close = new RegExp(`^\\s*${fence[0] === '`' ? '`' : '~'}{${fence.length},}\\s*$`);
      for (i++; i < lines.length && !close.test(lines[i]); i++) {
        body.push(lines[i].startsWith(indent) ? lines[i].slice(indent.length) : lines[i]);
      }
      let k = start - 1;
      while (k >= 0 && !lines[k].trim()) k--;
      const c = k >= 0 && /^\s*<!--([\s\S]*?)-->\s*$/.exec(lines[k]);
      out.push({ lang, code: body.join('\n'), line: start + 1, opt: parseOptions(c ? c[1] : '') });
    }
    return out;
  }

  return {
    classify, parseOptions, compileArgs, compileFiles, diagnostics, normLang, readsStdin, hash, FILE, ALLOWED,
    splitComment, normLine, typingLines, matchPrefix, blanks, blankCode, BLANK_MARK, unstable, normOutput, compareOutput, extractBlocks
  };
});
