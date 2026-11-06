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
 *                                       检查脚本反过来要求它出错
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

  return { classify, parseOptions, compileArgs, compileFiles, diagnostics, normLang, readsStdin, hash, FILE, ALLOWED };
});
