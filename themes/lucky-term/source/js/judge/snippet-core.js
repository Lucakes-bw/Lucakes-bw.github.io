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
      else if (/^(def|main|hidden|skip)$/.test(key) && val === undefined) opt.cell = key;   // notebook 里的角色
      else if (key === 'loose') opt.loose = true;      // 文中的输出是节选的：检查脚本不和它逐行比
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
    let details = 0;
    for (let i = 0; i < lines.length; i++) {
      details += (lines[i].match(/<details\b/g) || []).length - (lines[i].match(/<\/details>/g) || []).length;
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
      out.push({ lang, code: body.join('\n'), line: start + 1, opt: Object.assign(parseOptions(c ? c[1] : ''), details > 0 ? { details: true } : null) });
    }
    return out;
  }

  /* ---------- Notebook：一篇文章的代码块连成一个程序（front-matter 写 notebook: true） ----------
   * 像 Jupyter 一样，后面的代码块能用前面定义的函数和类型。C 没有交互式内核，所以每次运行都把
   * 第 1～N 格拼成一个完整程序重新编译：
   *   定义格（函数、类型、全局变量）原样放在顶层；
   *   语句格（「测试一下」那种）按顺序放进同一个 main，变量在后面的格子里还能用；
   *   每个语句格前打印一个分隔标记，跑完按标记把输出切回各个格子。
   * 文中省略的头文件自动补上；#include 统一提到最前面。
   * 每格用 #line 标成 cell<序号>.c，编译错误能对应回是哪一格的第几行。
   *
   * 作者用注释覆盖自动判定（和 run 的其他选项写在一起，比如 <!-- run: def -->）：
   *   def     当定义格（放在顶层）       main    当语句格（放进 main）
   *   hidden  辅助代码：文中没写出来、运行要用到的（页面上默认折叠）
   *   skip    不参与拼接（比如「错误示范」、伪代码、和前文冲突的另一种写法）
 *   loose   语句格后面文中给的输出是节选的，检查脚本不逐行对比（可以和 main 写在一起：<!-- run: main loose -->）
   * 自带 main 的代码块不参与拼接，照旧单独运行；<details> 里的（练习答案）默认也不参与。
   */
  const NB_MARK = '\x1e';
  const C_PRELUDE = ['stdio.h', 'stdlib.h', 'string.h', 'stdbool.h', 'stdint.h', 'limits.h', 'assert.h', 'ctype.h', 'math.h', 'time.h'];
  const CPP_PRELUDE = ['cstdio', 'cstdlib', 'cstring', 'climits', 'cassert', 'cmath', 'ctime', 'iostream', 'iomanip', 'sstream', 'string',
    'vector', 'list', 'deque', 'array', 'map', 'set', 'unordered_map', 'unordered_set', 'queue', 'stack', 'algorithm', 'numeric',
    'functional', 'memory', 'utility', 'tuple', 'optional', 'limits', 'random', 'chrono'];
  const NOT_TYPE = /^(return|goto|case|else|do|break|continue|sizeof|delete|throw|co_return|co_yield|co_await)$/;

  // 顶层（花括号外）一段段的语句：{ text: 开头到 ; 或配对的 }, body: 有没有花括号 }
  function topChunks(code) {
    const src = strip(code).replace(/^\s*#.*$/gm, '');
    const out = [];
    let depth = 0, cur = '', body = false, init = false;
    const end = () => { out.push({ text: cur.trim(), body }); cur = ''; body = false; init = false; };
    for (const ch of src) {
      cur += ch;
      if (ch === '{') { depth++; body = true; } else if (ch === '}') {
        depth--;
        // 函数体的 } 结束一段；struct {...} x; 和顶层的初始化列表 = {...}; 要等到分号
        if (depth === 0 && !init && !/^\s*(typedef|struct|union|enum|class)\b/.test(cur)) end();
      } else if (ch === '=' && depth === 0) init = true;
      else if (ch === ';' && depth === 0) end();
    }
    if (cur.trim()) out.push({ text: cur.trim(), body, open: true });
    return out;
  }
  // 按行把代码切成顶层的一段段：[{ start: 起始行（从 0 开始）, text }]。花括号回到 0 层、行尾是 ; 或 } 就是一段的结束
  function lineChunks(code) {
    const L = code.split('\n');
    const out = [];
    let depth = 0, start = 0, inBlock = false;
    L.forEach((line, i) => {
      let t = line;
      if (inBlock) { const e = t.indexOf('*/'); if (e < 0) return; t = t.slice(e + 2); inBlock = false; }
      t = t.replace(/\/\*[\s\S]*?\*\//g, ' ');
      const b = t.indexOf('/*');
      if (b >= 0) { t = t.slice(0, b); inBlock = true; }
      t = strip(t);
      for (const ch of t) { if (ch === '{') depth++; else if (ch === '}') depth--; }
      if (depth === 0 && /[;}]\s*$/.test(t) && !/^\s*#/.test(t)) { out.push({ start, text: L.slice(start, i + 1).join('\n') }); start = i + 1; }
    });
    if (start < L.length && L.slice(start).join('').trim()) out.push({ start, text: L.slice(start).join('\n') });
    return out;
  }
  // 一段顶层代码是不是声明或定义（否则就是要放进 main 的语句）
  function isDecl(t) {
    if (/^(typedef|struct|union|enum|class|template|namespace|using|extern|static_assert)\b/.test(t)) return true;
    // 函数定义：头部以 ) 结尾（可带 const / noexcept / override），不是 if / for / while
    const head = t.split('{')[0].trim();
    if (t.includes('{') && /\)\s*(const|noexcept|override|final|\s)*$/.test(head) && !/^(if|for|while|switch|do|else)\b/.test(head)) {
      return /^[\w:<>,\s*&~]+\(/.test(head) && !/^\w+\s*\(/.test(head) || /^[\w:]+::~?\w+\s*\(/.test(head);
    }
    // 变量声明 / 函数原型：类型 名字 ...
    const m = /^((?:static|const|constexpr|inline|unsigned|signed|volatile|long|short|struct|enum|union)\s+)*([A-Za-z_][\w:]*(?:\s*<[^;=(]*>)?)[\s*&]+([A-Za-z_]\w*)\s*(\[[^\]]*\]\s*)*(=|;|,|\(|\{)/.exec(t);
    return !!m && !NOT_TYPE.test(m[2]);
  }
  // 一个代码块在 notebook 里的角色：def / main / hidden / skip / alone（自带 main，单独运行）
  // opt.details：代码块在 <details> 里（通常是练习答案，用到的上下文文中没给全）——默认不参与，作者可以用 def / main 打开
  function cellKind(lang, code, opt = {}) {
    if (opt.cell) return opt.cell;
    if (opt.details) return 'skip';
    if (!normLang(lang) || opt.mode === 'off' || opt.error) return 'skip';
    if (/\bmain\s*\(/.test(strip(code))) return 'alone';
    for (const inc of includes(code)) if (inc.local || !ALLOWED.has(inc.name)) return 'skip';
    const chunks = topChunks(code);
    if (!chunks.length) return 'skip';
    if (chunks.some(c => c.open)) return 'skip';             // 花括号没配对：残缺的片段
    return chunks.every(c => isDecl(c.text)) ? 'def' : 'main';
  }

  // cells: [{ kind, code, name }]，按文章顺序；upto: 拼到第几格（下标，含）；返回 { source, mains }
  // mains 是参与的语句格下标，输出按它们的顺序切分
  function assemble(lang, cells, upto = cells.length - 1) {
    const use = cells.slice(0, upto + 1).map((c, i) => Object.assign({ i }, c)).filter(c => c.kind === 'def' || c.kind === 'hidden' || c.kind === 'main');
    const ext = lang === 'c' ? 'c' : 'cpp';
    const incs = new Set(lang === 'c' ? C_PRELUDE : CPP_PRELUDE);
    const pull = code => code.replace(/^\s*#\s*include\s*<([^>]+)>.*$/gm, (_, h) => { incs.add(h.trim()); return ''; });
    const body = use.map(c => Object.assign({}, c, { code: pull(c.code) }));
    const all = body.map(c => c.code).join('\n');
    const lines = [`#line 1 "prelude.${ext}"`, ...[...incs].map(h => `#include <${h}>`)];
    if (lang === 'cpp' && /(?<![:\w])(cout|cin|endl|vector|string)\b/.test(strip(all)) && !/using\s+namespace\s+std/.test(all)) lines.push('using namespace std;');
    const mark = i => `fputs("\\036${i}\\036", stdout);`;   // \036 就是 \x1e；用八进制，后面紧跟数字也不会被吞进转义
    const at = c => `#line 1 "${c.name}.${ext}"`;
    // 文章里常常先用后写（比如插入里调用后面才讲的扩容）：C 要求先声明。
    // 把顶层代码按行切成一段段（函数、类型、全局变量），给后面才定义的函数补一个原型，
    // 放在第一次用到它的那一段前面——那时它参数里的类型已经定义过了
    const segs = [];
    body.filter(c => c.kind !== 'main').forEach(c => lineChunks(c.code).forEach(g => segs.push(Object.assign({ cell: c, protos: [] }, g))));
    segs.forEach((g, k) => {
      for (const ch of topChunks(g.text)) {
        if (!ch.body || !isDecl(ch.text)) continue;
        const head = ch.text.split('{')[0].trim();
        if (!/\)\s*$/.test(head) || /^(typedef|struct|union|enum|class|template)\b/.test(head)) continue;
        const m = /([A-Za-z_]\w*)\s*\(/.exec(head.replace(/^((static|inline|const|unsigned|signed|struct|enum)\s+)+/, ''));
        if (!m) continue;
        const re = new RegExp(`\\b${m[1]}\\s*\\(`);
        const j = segs.findIndex((d, jj) => jj < k && re.test(strip(d.text)));
        if (j >= 0) segs[j].protos.push(head + ';');
      }
    });
    segs.forEach(g => {
      if (g.protos.length) lines.push(`#line ${g.start + 1} "${g.cell.name}.${ext}"`, ...g.protos);
      lines.push(`#line ${g.start + 1} "${g.cell.name}.${ext}"`, g.text);
    });
    const mains = body.filter(c => c.kind === 'main');
    lines.push(`#line 1 "main.${ext}"`, 'int main(void) {');
    mains.forEach(c => lines.push(mark(c.i), at(c), c.code));
    lines.push(`#line 1 "main.${ext}"`, `${mark(-1)} return 0;`, '}', '');
    return { source: lines.join('\n'), mains: mains.map(c => c.i) };
  }
  // 拼好的程序的输出 → { 格子下标: 文本 }；last 是最后一个开始运行的格子（运行出错时用来定位）
  function splitOutput(stdout) {
    const parts = String(stdout || '').split(/\x1e(-?\d+)\x1e/);
    const out = {};
    let last = null;
    for (let k = 1; k < parts.length; k += 2) {
      const i = +parts[k];
      if (i < 0) { last = -1; break; }
      out[i] = (out[i] || '') + parts[k + 1];
      last = i;
    }
    return { out, last, before: parts[0] };
  }
  const stripMarks = s => String(s || '').replace(/\x1e-?\d+\x1e/g, '');
  // notebook 用哪种语言编译：参与拼接的格子里多数是哪种（另一种语言的格子不参与）
  function notebookLang(cells) {
    const use = cells.filter(c => /^(def|main|hidden)$/.test(c.kind));
    return use.filter(c => normLang(c.lang) === 'cpp').length > use.length / 2 ? 'cpp' : 'c';
  }
  // 拼接后的诊断：[{ cell: 文件名（cell3 等）, line, col, severity, msg }]
  function nbDiagnostics(text) {
    const out = [];
    const re = /^([\w-]+)\.(?:c|cpp):(\d+):(\d+): (error|warning): (.*)$/gm;
    let m;
    while ((m = re.exec(text || ''))) out.push({ cell: m[1], line: +m[2], col: +m[3], severity: m[4], msg: m[5] });
    return out;
  }

  return {
    classify, parseOptions, compileArgs, compileFiles, diagnostics, normLang, readsStdin, hash, FILE, ALLOWED,
    splitComment, blanks, blankCode, BLANK_MARK, unstable, normOutput, compareOutput, extractBlocks,
    cellKind, assemble, notebookLang, splitOutput, stripMarks, nbDiagnostics, NB_MARK
  };
});
