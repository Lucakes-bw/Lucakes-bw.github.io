/**
 * 刷题判题核心：浏览器（页面和 JS 的 Worker）和 Node（tools/check-problems.mjs）共用
 *
 * 题目签名 sig：{ function, params: [[name, type]], returns, output?, compare? }
 *   type：int / long / double / bool / string / char / ListNode / TreeNode，后面可以接若干个 []
 *   returns 为 void 时，output 指定调用后输出哪个参数（原地修改类的题目）
 *
 * 三种语言的驱动程序读同一份输入：第一行是用例数 T，之后每个用例每个参数一行 JSON。
 * 每跑完一个用例输出一行「\x1eRES 耗时ms 结果JSON」；出错输出「\x1eERR {msg,line}」，
 * 语法错误输出「\x1eCE {msg,line,col}」。不带 \x1e 的内容都是用户自己打印的。
 */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.LTJudge = factory();
})(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  const LANGS = {
    cpp: { name: 'C++', monaco: 'cpp', file: 'solution.cpp' },
    python: { name: 'Python3', monaco: 'python', file: 'solution.py' },
    javascript: { name: 'JavaScript', monaco: 'javascript', file: 'solution.js' }
  };

  function parseType(t) {
    const m = /^(\w+)((?:\[\])*)$/.exec(String(t).trim());
    if (!m) throw new Error('无法识别的类型：' + t);
    return { base: m[1], dim: m[2].length / 2 };
  }
  // 题目 front-matter → 签名。params 写成 { 参数名: 类型 }，按书写顺序
  function signature(fm) {
    const sig = {
      function: fm.function,
      params: Object.entries(fm.params || {}).map(([n, t]) => [n, String(t)]),
      returns: String(fm.returns || 'void'),
      compare: fm.compare || 'exact'
    };
    if (fm.output) sig.output = fm.output;
    [sig.returns].concat(sig.params.map(p => p[1])).forEach(t => { if (t !== 'void') parseType(t); });
    if (sig.returns === 'void' && !sig.params.some(p => p[0] === sig.output)) throw new Error('returns 为 void 时需要用 output 指定输出的参数');
    return sig;
  }

  const uses = (sig, base) => sig.params.some(p => parseType(p[1]).base === base) ||
    (sig.returns !== 'void' && parseType(sig.returns).base === base);

  /* ---------- 代码模板 ---------- */

  const CPP_BASE = { int: 'int', long: 'long long', double: 'double', bool: 'bool', string: 'string', char: 'char', ListNode: 'ListNode*', TreeNode: 'TreeNode*', void: 'void' };
  const PY_BASE = { int: 'int', long: 'int', double: 'float', bool: 'bool', string: 'str', char: 'str', ListNode: 'Optional[ListNode]', TreeNode: 'Optional[TreeNode]', void: 'None' };
  const JS_BASE = { int: 'number', long: 'number', double: 'number', bool: 'boolean', string: 'string', char: 'character', ListNode: 'ListNode', TreeNode: 'TreeNode', void: 'void' };

  function cppType(t) {
    const { base, dim } = parseType(t);
    let s = CPP_BASE[base];
    for (let i = 0; i < dim; i++) s = `vector<${s}>`;
    return s;
  }
  function pyType(t) {
    const { base, dim } = parseType(t);
    let s = PY_BASE[base];
    for (let i = 0; i < dim; i++) s = `List[${s}]`;
    return s;
  }
  const jsType = t => { const { base, dim } = parseType(t); return JS_BASE[base] + '[]'.repeat(dim); };

  const DEFS = {
    cpp: {
      ListNode: `/**
 * Definition for singly-linked list.
 * struct ListNode {
 *     int val;
 *     ListNode *next;
 *     ListNode() : val(0), next(nullptr) {}
 *     ListNode(int x) : val(x), next(nullptr) {}
 *     ListNode(int x, ListNode *next) : val(x), next(next) {}
 * };
 */`,
      TreeNode: `/**
 * Definition for a binary tree node.
 * struct TreeNode {
 *     int val;
 *     TreeNode *left;
 *     TreeNode *right;
 *     TreeNode() : val(0), left(nullptr), right(nullptr) {}
 *     TreeNode(int x) : val(x), left(nullptr), right(nullptr) {}
 *     TreeNode(int x, TreeNode *left, TreeNode *right) : val(x), left(left), right(right) {}
 * };
 */`
    },
    python: {
      ListNode: `# Definition for singly-linked list.
# class ListNode:
#     def __init__(self, val=0, next=None):
#         self.val = val
#         self.next = next`,
      TreeNode: `# Definition for a binary tree node.
# class TreeNode:
#     def __init__(self, val=0, left=None, right=None):
#         self.val = val
#         self.left = left
#         self.right = right`
    },
    javascript: {
      ListNode: `/**
 * Definition for singly-linked list.
 * function ListNode(val, next) {
 *     this.val = (val===undefined ? 0 : val)
 *     this.next = (next===undefined ? null : next)
 * }
 */`,
      TreeNode: `/**
 * Definition for a binary tree node.
 * function TreeNode(val, left, right) {
 *     this.val = (val===undefined ? 0 : val)
 *     this.left = (left===undefined ? null : left)
 *     this.right = (right===undefined ? null : right)
 * }
 */`
    }
  };

  function template(lang, sig) {
    const defs = ['ListNode', 'TreeNode'].filter(b => uses(sig, b)).map(b => DEFS[lang][b] + '\n');
    const head = defs.join('');
    if (lang === 'cpp') {
      const args = sig.params.map(([n, t]) => {
        const { base, dim } = parseType(t);
        return `${cppType(t)}${dim || base === 'string' ? '&' : ''} ${n}`;
      }).join(', ');
      return `${head}class Solution {
public:
    ${cppType(sig.returns)} ${sig.function}(${args}) {

    }
};
`;
    }
    if (lang === 'python') {
      const args = ['self'].concat(sig.params.map(([n, t]) => `${n}: ${pyType(t)}`)).join(', ');
      return `${head}class Solution:
    def ${sig.function}(${args}) -> ${pyType(sig.returns)}:

`;
    }
    const doc = sig.params.map(([n, t]) => ` * @param {${jsType(t)}} ${n}`).join('\n');
    const ret = sig.returns === 'void'
      ? ` * @return {void} Do not return anything, modify ${sig.output} in-place instead.`
      : ` * @return {${jsType(sig.returns)}}`;
    return `${head}/**
${doc}
${ret}
 */
var ${sig.function} = function(${sig.params.map(p => p[0]).join(', ')}) {

};
`;
  }

  /* ---------- C++：拼成一个完整的程序 ---------- */

  // 浏览器里没有 GCC 的 <bits/stdc++.h>，编译时用这份代替
  const CPP_BITS = ['algorithm', 'array', 'bitset', 'cassert', 'cctype', 'cfloat', 'climits', 'cmath', 'complex', 'cstdint',
    'cstdio', 'cstdlib', 'cstring', 'deque', 'functional', 'iomanip', 'iostream', 'iterator', 'limits', 'list', 'map',
    'memory', 'numeric', 'optional', 'queue', 'random', 'set', 'sstream', 'stack', 'string', 'string_view', 'tuple',
    'unordered_map', 'unordered_set', 'utility', 'vector', 'chrono', 'bit', 'span', 'variant', 'ranges', 'numbers']
    .map(h => `#include <${h}>`).join('\n') + '\n';

  // 编译器：YoWASP 打包的 Clang/LLD（WASI 目标，自带 libc++，不支持异常）
  const CLANG = { pkg: '@yowasp/clang', version: '22.0.0-git20542-10' };
  const CPP_FLAGS = ['-std=c++20', '-O2', '-fno-exceptions', '-I/lt', '-Wl,-z,stack-size=8388608', '-Wl,--max-memory=536870912'];
  const cppFiles = source => ({ 'main.cpp': source, lt: { bits: { 'stdc++.h': CPP_BITS } } });

  const CPP_PRELUDE = `#include <bits/stdc++.h>
using namespace std;
struct ListNode { int val; ListNode *next; ListNode() : val(0), next(nullptr) {} ListNode(int x) : val(x), next(nullptr) {} ListNode(int x, ListNode *next) : val(x), next(next) {} };
struct TreeNode { int val; TreeNode *left; TreeNode *right; TreeNode() : val(0), left(nullptr), right(nullptr) {} TreeNode(int x) : val(x), left(nullptr), right(nullptr) {} TreeNode(int x, TreeNode *left, TreeNode *right) : val(x), left(left), right(right) {} };
`;

  // 简单的 JSON 解析 / 输出。函数都放在命名空间里并带限定名调用，避免和用户代码重名
  const CPP_DRIVER = `
namespace lt_judge {
struct J { char t = 'n'; std::string s; std::vector<J> a; };
struct Parser {
  const std::string& s; size_t p = 0;
  explicit Parser(const std::string& str) : s(str) {}
  void ws() { while (p < s.size() && isspace((unsigned char)s[p])) p++; }
  J val() {
    ws(); J j; if (p >= s.size()) return j;
    char c = s[p];
    if (c == '[') {
      p++; j.t = 'a'; ws();
      if (p < s.size() && s[p] == ']') { p++; return j; }
      while (p < s.size()) { j.a.push_back(val()); ws(); if (p < s.size() && s[p] == ',') { p++; continue; } p++; break; }
      return j;
    }
    if (c == '"') {
      p++; j.t = 's';
      while (p < s.size() && s[p] != '"') {
        char ch = s[p++];
        if (ch == '\\\\' && p < s.size()) {
          char e = s[p++];
          if (e == 'u') {
            unsigned cp = (unsigned)strtoul(s.substr(p, 4).c_str(), nullptr, 16); p += 4;
            if (cp < 0x80) j.s += (char)cp;
            else if (cp < 0x800) { j.s += (char)(0xC0 | (cp >> 6)); j.s += (char)(0x80 | (cp & 63)); }
            else { j.s += (char)(0xE0 | (cp >> 12)); j.s += (char)(0x80 | ((cp >> 6) & 63)); j.s += (char)(0x80 | (cp & 63)); }
            continue;
          }
          ch = e == 'n' ? '\\n' : e == 't' ? '\\t' : e == 'r' ? '\\r' : e == 'b' ? '\\b' : e == 'f' ? '\\f' : e;
        }
        j.s += ch;
      }
      p++; return j;
    }
    size_t b = p;
    while (p < s.size() && !strchr(",]} \\t\\r\\n", s[p])) p++;
    j.s = s.substr(b, p - b); j.t = j.s == "null" ? 'n' : 'v';
    return j;
  }
};

inline void get(const J& j, int& v) { v = (int)strtoll(j.s.c_str(), nullptr, 10); }
inline void get(const J& j, long long& v) { v = strtoll(j.s.c_str(), nullptr, 10); }
inline void get(const J& j, double& v) { v = strtod(j.s.c_str(), nullptr); }
inline void get(const J& j, bool& v) { v = j.s == "true"; }
inline void get(const J& j, std::string& v) { v = j.s; }
inline void get(const J& j, char& v) { v = j.s.empty() ? '\\0' : j.s[0]; }
inline void get(const J& j, ListNode*& v) {
  ListNode d; ListNode* t = &d;
  for (const J& x : j.a) { int n; get(x, n); t->next = new ListNode(n); t = t->next; }
  v = d.next;
}
inline void get(const J& j, TreeNode*& v) {
  v = nullptr;
  if (j.a.empty() || j.a[0].t == 'n') return;
  auto num = [](const J& x) { int n; get(x, n); return n; };
  v = new TreeNode(num(j.a[0]));
  std::queue<TreeNode*> q; q.push(v);
  size_t i = 1;
  while (!q.empty() && i < j.a.size()) {
    TreeNode* n = q.front(); q.pop();
    if (i < j.a.size() && j.a[i].t != 'n') { n->left = new TreeNode(num(j.a[i])); q.push(n->left); }
    i++;
    if (i < j.a.size() && j.a[i].t != 'n') { n->right = new TreeNode(num(j.a[i])); q.push(n->right); }
    i++;
  }
}
inline void get(const J& j, std::vector<bool>& v) { v.assign(j.a.size(), false); for (size_t k = 0; k < j.a.size(); k++) v[k] = j.a[k].s == "true"; }
template <class T> void get(const J& j, std::vector<T>& v) { v.resize(j.a.size()); for (size_t k = 0; k < j.a.size(); k++) get(j.a[k], v[k]); }

inline void put(std::ostream& o, int v) { o << v; }
inline void put(std::ostream& o, long v) { o << v; }
inline void put(std::ostream& o, long long v) { o << v; }
inline void put(std::ostream& o, unsigned v) { o << v; }
inline void put(std::ostream& o, unsigned long v) { o << v; }
inline void put(std::ostream& o, unsigned long long v) { o << v; }
inline void put(std::ostream& o, double v) {
  if (!std::isfinite(v)) { o << "null"; return; }
  std::ostringstream ss; ss << std::setprecision(12) << v; o << ss.str();
}
inline void put(std::ostream& o, bool v) { o << (v ? "true" : "false"); }
inline void put(std::ostream& o, const std::string& v) {
  o << '"';
  for (unsigned char c : v) {
    if (c == '"' || c == '\\\\') o << '\\\\' << c;
    else if (c == '\\n') o << "\\\\n";
    else if (c == '\\t') o << "\\\\t";
    else if (c == '\\r') o << "\\\\r";
    else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\\\u%04x", c); o << b; }
    else o << c;
  }
  o << '"';
}
inline void put(std::ostream& o, char v) { put(o, std::string(1, v)); }
inline void put(std::ostream& o, ListNode* v) {
  o << '[';
  for (int k = 0; v && k < 200000; k++, v = v->next) { if (k) o << ','; o << v->val; }
  o << ']';
}
inline void put(std::ostream& o, TreeNode* r) {
  std::vector<TreeNode*> q;
  if (r) q.push_back(r);
  for (size_t i = 0; i < q.size() && q.size() < 400000; i++) if (q[i]) { q.push_back(q[i]->left); q.push_back(q[i]->right); }
  while (!q.empty() && !q.back()) q.pop_back();
  o << '[';
  for (size_t i = 0; i < q.size(); i++) { if (i) o << ','; if (q[i]) o << q[i]->val; else o << "null"; }
  o << ']';
}
inline void put(std::ostream& o, const std::vector<bool>& v) { o << '['; for (size_t k = 0; k < v.size(); k++) { if (k) o << ','; o << (v[k] ? "true" : "false"); } o << ']'; }
template <class T> void put(std::ostream& o, const std::vector<T>& v) { o << '['; for (size_t k = 0; k < v.size(); k++) { if (k) o << ','; put(o, v[k]); } o << ']'; }

inline J line() { static std::string s; std::getline(std::cin, s); return Parser(s).val(); }
}
`;

  function cppProgram(sig, code) {
    const decl = sig.params.map(([n, t], i) => `    ${cppType(t)} a${i}; lt_judge::get(lt_judge::line(), a${i});`).join('\n');
    const args = sig.params.map((p, i) => `a${i}`).join(', ');
    const outIdx = sig.params.findIndex(p => p[0] === sig.output);
    const call = sig.returns === 'void'
      ? `Solution().${sig.function}(${args});`
      : `auto r = Solution().${sig.function}(${args});`;
    const result = sig.returns === 'void' ? `a${outIdx}` : 'r';
    return `${CPP_PRELUDE}#line 1 "solution.cpp"
${code}
#line 1 "lt_judge.cpp"
${CPP_DRIVER}
int main() {
  std::string head; std::getline(std::cin, head);
  int T = atoi(head.c_str());
  for (int t = 0; t < T; t++) {
${decl}
    auto t0 = std::chrono::steady_clock::now();
    ${call}
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::cout << std::flush << "\\036RES " << ms << " ";
    lt_judge::put(std::cout, ${result});
    std::cout << std::endl;
  }
  return 0;
}
`;
  }

  /* ---------- Python：驱动是固定的，签名作为参数传进去 ---------- */

  const PY_DRIVER = `
import sys, json, time, traceback

_LT_PRELUDE = '''
from typing import *
import collections, heapq, bisect, itertools, functools, math, string, re, random, operator
from collections import *
from heapq import *
from bisect import *
from itertools import *
from functools import *
inf = float('inf')
class ListNode:
    def __init__(self, val=0, next=None):
        self.val = val
        self.next = next
class TreeNode:
    def __init__(self, val=0, left=None, right=None):
        self.val = val
        self.left = left
        self.right = right
'''

def _lt_tb(e):
    frames = [f for f in traceback.extract_tb(e.__traceback__) if f.filename == 'solution.py']
    lines = ['Line %d in %s' % (f.lineno, f.name) for f in frames[-3:]]
    lines.append('%s: %s' % (type(e).__name__, e))
    return {'msg': '\\n'.join(lines), 'line': frames[-1].lineno if frames else 0}

def _lt_judge(code, sig_json, inp):
    sig = json.loads(sig_json)
    ns = {'__name__': '__main__'}
    exec(_LT_PRELUDE, ns)
    ListNode, TreeNode = ns['ListNode'], ns['TreeNode']

    def de(v, t):
        base, dim = t.rstrip('[]'), (len(t) - len(t.rstrip('[]'))) // 2
        if dim:
            return [de(x, t[:-2]) for x in v]
        if base == 'ListNode':
            d = cur = ListNode()
            for x in v:
                cur.next = ListNode(x)
                cur = cur.next
            return d.next
        if base == 'TreeNode':
            if not v or v[0] is None:
                return None
            root = TreeNode(v[0]); q = [root]; i = 1; h = 0
            while h < len(q) and i < len(v):
                n = q[h]; h += 1
                if i < len(v) and v[i] is not None:
                    n.left = TreeNode(v[i]); q.append(n.left)
                i += 1
                if i < len(v) and v[i] is not None:
                    n.right = TreeNode(v[i]); q.append(n.right)
                i += 1
            return root
        if base == 'double':
            return float(v)
        return v

    # 按签名里的类型输出：返回 None 的链表 / 树要输出成 []
    def ser(v, t):
        base, dim = t.rstrip('[]'), (len(t) - len(t.rstrip('[]'))) // 2
        if dim:
            return [ser(x, t[:-2]) for x in v] if isinstance(v, (list, tuple)) else v
        if base == 'ListNode':
            out = []
            while v is not None and len(out) < 200000:
                out.append(v.val); v = v.next
            return out
        if base == 'TreeNode':
            q = [v] if v is not None else []
            i = 0
            while i < len(q) and len(q) < 400000:
                if q[i] is not None:
                    q.append(q[i].left); q.append(q[i].right)
                i += 1
            while q and q[-1] is None:
                q.pop()
            return [None if n is None else n.val for n in q]
        return v

    try:
        exec(compile(code, 'solution.py', 'exec'), ns)
    except SyntaxError as e:
        print('\\x1eCE ' + json.dumps({'msg': '%s: %s' % (type(e).__name__, e.msg), 'line': e.lineno or 0, 'col': e.offset or 0}))
        return
    except BaseException as e:
        print('\\x1eERR ' + json.dumps(_lt_tb(e)))
        return
    if 'Solution' not in ns:
        print('\\x1eERR ' + json.dumps({'msg': '没有找到 class Solution', 'line': 0}))
        return

    sys.setrecursionlimit(20000)
    lines = inp.split('\\n')
    names = [p[0] for p in sig['params']]
    k = 1
    for _ in range(int(lines[0])):
        args = []
        for name, t in sig['params']:
            args.append(de(json.loads(lines[k]), t)); k += 1
        try:
            fn = getattr(ns['Solution'](), sig['function'])
            t0 = time.perf_counter()
            r = fn(*args)
            ms = (time.perf_counter() - t0) * 1000
            if sig.get('output'):
                i = names.index(sig['output'])
                r, rt = args[i], sig['params'][i][1]
            else:
                rt = sig['returns']
            out = json.dumps(ser(r, rt), separators=(',', ':'), ensure_ascii=False)
            sys.stdout.flush()
            print('\\x1eRES %.3f %s' % (ms, out))
        except BaseException as e:
            print('\\x1eERR ' + json.dumps(_lt_tb(e), ensure_ascii=False))
`;

  /* ---------- JavaScript：在 Worker（或单独的 Node 进程）里直接执行 ---------- */

  function fmtLog(args) {
    return args.map(a => {
      if (typeof a === 'string') return a;
      try {
        const seen = new Set();
        return JSON.stringify(a, (k, v) => {
          if (typeof v === 'object' && v) { if (seen.has(v)) return '[Circular]'; seen.add(v); }
          if (typeof v === 'bigint') return v.toString();
          return v;
        }) ?? String(a);
      } catch (e) { return String(a); }
    }).join(' ');
  }

  // write(text) 收集输出；console 由调用方提前接到 write 上
  function runJs(code, sig, input, write) {
    const g = (0, eval);
    const G = typeof globalThis !== 'undefined' ? globalThis : self;
    G.ListNode = function ListNode(val, next) { this.val = val === undefined ? 0 : val; this.next = next === undefined ? null : next; };
    G.TreeNode = function TreeNode(val, left, right) { this.val = val === undefined ? 0 : val; this.left = left === undefined ? null : left; this.right = right === undefined ? null : right; };
    const errInfo = e => {
      const m = /solution\.js:(\d+)/.exec((e && e.stack) || '');
      return { msg: e && e.name ? `${e.name}: ${e.message}` : String(e), line: m ? +m[1] : 0 };
    };
    try {
      g(code + '\n//# sourceURL=solution.js');
    } catch (e) {
      write((e instanceof SyntaxError ? '\x1eCE ' : '\x1eERR ') + JSON.stringify(errInfo(e)) + '\n');
      return;
    }
    let fn;
    try { fn = g(sig.function); } catch (e) { /* 下面统一报错 */ }
    if (typeof fn !== 'function') {
      write('\x1eERR ' + JSON.stringify({ msg: `没有找到函数 ${sig.function}`, line: 0 }) + '\n');
      return;
    }
    const de = (v, t) => {
      const { base, dim } = parseType(t);
      if (dim) return v.map(x => de(x, t.slice(0, -2)));
      if (base === 'ListNode') {
        const d = new G.ListNode(); let cur = d;
        v.forEach(x => { cur.next = new G.ListNode(x); cur = cur.next; });
        return d.next;
      }
      if (base === 'TreeNode') {
        if (!v.length || v[0] === null) return null;
        const root = new G.TreeNode(v[0]); const q = [root]; let i = 1, h = 0;
        while (h < q.length && i < v.length) {
          const n = q[h++];
          if (i < v.length && v[i] !== null) { n.left = new G.TreeNode(v[i]); q.push(n.left); }
          i++;
          if (i < v.length && v[i] !== null) { n.right = new G.TreeNode(v[i]); q.push(n.right); }
          i++;
        }
        return root;
      }
      return v;
    };
    const ser = (v, t) => {
      const { base, dim } = parseType(t);
      if (dim) return Array.isArray(v) ? v.map(x => ser(x, t.slice(0, -2))) : v;
      if (base === 'ListNode') {
        const out = [];
        while (v && out.length < 200000) { out.push(v.val); v = v.next; }
        return out;
      }
      if (base === 'TreeNode') {
        const q = v ? [v] : [];
        for (let i = 0; i < q.length && q.length < 400000; i++) if (q[i]) q.push(q[i].left, q[i].right);
        while (q.length && !q[q.length - 1]) q.pop();
        return q.map(n => (n ? n.val : null));
      }
      return v === undefined ? null : v;
    };
    const now = typeof performance !== 'undefined' ? () => performance.now() : () => Date.now();
    const lines = input.split('\n');
    const outIdx = sig.params.findIndex(p => p[0] === sig.output);
    let k = 1;
    for (let t = +lines[0]; t > 0; t--) {
      const args = sig.params.map(([, type]) => de(JSON.parse(lines[k++]), type));
      try {
        const t0 = now();
        let r = fn(...args);
        const ms = now() - t0;
        r = sig.returns === 'void' ? ser(args[outIdx], sig.params[outIdx][1]) : ser(r, sig.returns);
        write(`\x1eRES ${ms.toFixed(3)} ${JSON.stringify(r)}\n`);
      } catch (e) {
        write('\x1eERR ' + JSON.stringify(errInfo(e)) + '\n');
      }
    }
  }

  /* ---------- 输入、输出和比较 ---------- */

  const encodeInput = cases => [String(cases.length)]
    .concat(...cases.map(c => c.in.map(v => JSON.stringify(v)))).join('\n') + '\n';

  function parseOutput(stdout) {
    const results = [];
    let buf = '';
    let ce = null;
    stdout.split('\n').forEach((line, i, all) => {
      const at = line.indexOf('\x1e');
      if (at < 0) { if (i < all.length - 1 || line) buf += line + (i < all.length - 1 ? '\n' : ''); return; }
      buf += line.slice(0, at);
      const m = /^(RES|ERR|CE) (.*)$/.exec(line.slice(at + 1));
      if (!m) return;
      if (m[1] === 'RES') {
        const sp = m[2].indexOf(' ');
        results.push({ ms: parseFloat(m[2].slice(0, sp)), out: m[2].slice(sp + 1), stdout: buf });
      } else if (m[1] === 'ERR') {
        results.push({ error: JSON.parse(m[2]), stdout: buf });
      } else {
        ce = JSON.parse(m[2]);
      }
      buf = '';
    });
    return { results, rest: buf, ce };
  }

  function canon(v, deep) {
    if (!Array.isArray(v)) return v;
    const arr = deep ? v.map(x => canon(x, true)) : v.slice();
    return arr.map(x => [JSON.stringify(x), x]).sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0)).map(x => x[1]);
  }
  function equal(a, b, float) {
    if (float && typeof a === 'number' && typeof b === 'number') return Math.abs(a - b) <= 1e-5 * Math.max(1, Math.abs(b));
    if (Array.isArray(a) && Array.isArray(b)) return a.length === b.length && a.every((x, i) => equal(x, b[i], float));
    return a === b;
  }
  // compare：exact（默认）/ unordered（最外层不计顺序）/ unordered-deep（每一层都不计顺序）/ float（允许 1e-5 误差）
  function compare(mode, got, expected) {
    if (mode === 'unordered') return equal(canon(got, false), canon(expected, false));
    if (mode === 'unordered-deep') return equal(canon(got, true), canon(expected, true));
    return equal(got, expected, mode === 'float');
  }

  /**
   * 判题
   * cases：[{ in: [...], out?: ... }]，没有 out 的用例只显示输出（自定义用例）
   * run：{ stdout, stderr, compileError?, runtimeError?, timeout? }
   * 返回 { status, cases: [{ status, got, stdout, ms, error }], compileError }
   * status：AC 通过 / WA 解答错误 / RE 执行出错 / TLE 超时 / CE 编译出错 / DONE 运行完成（无预期结果）
   */
  function evaluate(sig, cases, run) {
    if (run.compileError) return { status: 'CE', cases: [], compileError: run.compileError };
    const { results, rest, ce } = parseOutput(run.stdout || '');
    if (ce) return { status: 'CE', cases: [], compileError: ce.msg, diagnostics: [{ line: ce.line, col: ce.col, msg: ce.msg, severity: 'error' }] };
    const out = cases.map((c, i) => {
      const r = results[i];
      if (!r) return { status: 'SKIP' };
      if (r.error) return { status: 'RE', error: r.error.msg, line: r.error.line, stdout: r.stdout };
      let got;
      try { got = JSON.parse(r.out); } catch (e) { got = r.out; }
      const has = Object.prototype.hasOwnProperty.call(c, 'out');
      return { status: !has ? 'DONE' : compare(sig.compare, got, c.out) ? 'AC' : 'WA', got, stdout: r.stdout, ms: r.ms };
    });
    // 进程在第 n 个用例中途退出：这个用例记为出错 / 超时，并带上它打印的内容和错误信息
    const n = results.length;
    if (n < cases.length) {
      out[n] = {
        status: run.timeout ? 'TLE' : 'RE',
        error: run.timeout ? '超出时间限制' : (run.runtimeError || (run.stderr || '').trim() || '程序异常退出'),
        stdout: rest
      };
    }
    const first = out.find(c => c.status !== 'AC' && c.status !== 'DONE');
    return { status: first ? first.status : out.every(c => c.status === 'AC') ? 'AC' : 'DONE', cases: out };
  }

  return { LANGS, parseType, signature, template, cppProgram, CLANG, CPP_FLAGS, cppFiles, PY_DRIVER, runJs, fmtLog, encodeInput, parseOutput, compare, evaluate };
});
