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
  // 特殊输入：用例里的数据和函数参数不一一对应，由驱动程序按约定搭出链表 / 树再调用。
  // 这里是用例里每一项的名字和类型（做题页按它显示和编辑用例）
  const INPUTS = {
    cycle: [['head', 'int[]'], ['pos', 'int']],                                       // 尾节点接到下标 pos（-1 表示无环）
    intersect: [['listA', 'int[]'], ['listB', 'int[]'], ['skipA', 'int'], ['skipB', 'int']],  // B 的前 skipB 个节点之后接到 A 的第 skipA 个节点
    random: [['head', 'int[][]']],                                                     // 每个节点 [val, random 指向的下标或 null]
    lca: [['root', 'TreeNode'], ['p', 'int'], ['q', 'int']]                            // p、q 是节点的值（树中的值互不相同）
  };

  const entries = o => Object.entries(o || {}).map(([n, t]) => [n, String(t)]);

  /**
   * 题目 front-matter → 签名。params 写成 { 参数名: 类型 }，按书写顺序
   * 设计类的题目写 design: { class, init: {参数}, methods: { 方法名: { params, returns } } }，
   *   用例是 [操作名数组, 参数数组]，输出是每次调用的返回值（构造和 void 方法为 null）
   * 特殊输入写 input: cycle / intersect / random / lca，函数参数放到 sig.call，sig.params 换成用例的格式
   */
  function signature(fm) {
    const sig = {
      function: fm.function,
      params: entries(fm.params),
      returns: String(fm.returns || 'void'),
      compare: fm.compare || 'exact'
    };
    if (fm.output) sig.output = fm.output;
    const types = [];
    if (fm.design) {
      const d = fm.design;
      sig.design = {
        class: d.class,
        init: entries(d.init),
        methods: Object.entries(d.methods || {}).map(([name, m]) => ({ name, params: entries((m || {}).params), returns: String((m || {}).returns || 'void') }))
      };
      if (!d.class || !sig.design.methods.length) throw new Error('design 需要 class 和 methods');
      sig.function = d.class;
      sig.params = [['operations', 'string[]'], ['arguments', 'any[][]']];
      sig.returns = 'any[]';
      types.push(...usedTypes(sig));
    } else {
      if (fm.input) {
        if (!INPUTS[fm.input]) throw new Error('未知的 input：' + fm.input);
        sig.input = fm.input;
        sig.call = sig.params;
        sig.params = INPUTS[fm.input].map(p => p.slice());
      }
      types.push(...usedTypes(sig));
      if (sig.returns === 'void' && !sig.params.some(p => p[0] === sig.output)) throw new Error('returns 为 void 时需要用 output 指定输出的参数');
    }
    types.forEach(t => { if (t !== 'void' && !(parseType(t).base in CPP_BASE)) throw new Error('无法识别的类型：' + t); });
    return sig;
  }

  // 函数真正的参数（特殊输入的题目和用例格式不同）
  const callParams = sig => sig.call || sig.params;
  const usedTypes = sig => sig.design
    ? [...sig.design.init, ...sig.design.methods.flatMap(m => m.params)].map(p => p[1]).concat(sig.design.methods.map(m => m.returns))
    : callParams(sig).map(p => p[1]).concat(sig.returns);
  const uses = (sig, base) => usedTypes(sig).some(t => t !== 'void' && parseType(t).base === base);

  /* ---------- 代码模板 ---------- */

  const CPP_BASE = { int: 'int', long: 'long long', double: 'double', bool: 'bool', string: 'string', char: 'char', ListNode: 'ListNode*', TreeNode: 'TreeNode*', Node: 'Node*', void: 'void' };
  const PY_BASE = { int: 'int', long: 'int', double: 'float', bool: 'bool', string: 'str', char: 'str', ListNode: 'Optional[ListNode]', TreeNode: 'Optional[TreeNode]', Node: "'Optional[Node]'", void: 'None' };
  const JS_BASE = { int: 'number', long: 'number', double: 'number', bool: 'boolean', string: 'string', char: 'character', ListNode: 'ListNode', TreeNode: 'TreeNode', Node: '_Node', void: 'void' };

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
 */`,
      Node: `/*
// Definition for a Node.
class Node {
public:
    int val;
    Node* next;
    Node* random;

    Node(int _val) {
        val = _val;
        next = NULL;
        random = NULL;
    }
};
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
#         self.right = right`,
      Node: `"""
# Definition for a Node.
class Node:
    def __init__(self, x: int, next: 'Node' = None, random: 'Node' = None):
        self.val = int(x)
        self.next = next
        self.random = random
"""`
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
 */`,
      Node: `/**
 * // Definition for a _Node.
 * function _Node(val, next, random) {
 *    this.val = val;
 *    this.next = next;
 *    this.random = random;
 * };
 */`
    }
  };

  const cppArgs = params => params.map(([n, t]) => {
    const { base, dim } = parseType(t);
    return `${cppType(t)}${dim || base === 'string' ? '&' : ''} ${n}`;
  }).join(', ');
  const pyArgs = params => ['self'].concat(params.map(([n, t]) => `${n}: ${pyType(t)}`)).join(', ');
  const jsDoc = (params, returns) => params.map(([n, t]) => ` * @param {${jsType(t)}} ${n}`)
    .concat(returns === undefined ? [] : [` * @return {${jsType(returns)}}`]).join('\n');

  function template(lang, sig) {
    const defs = ['ListNode', 'TreeNode', 'Node'].filter(b => uses(sig, b)).map(b => DEFS[lang][b] + '\n');
    const head = defs.join('');
    if (sig.design) return head + designTemplate(lang, sig.design);
    const params = callParams(sig);
    if (lang === 'cpp') {
      return `${head}class Solution {
public:
    ${cppType(sig.returns)} ${sig.function}(${cppArgs(params)}) {

    }
};
`;
    }
    if (lang === 'python') {
      return `${head}class Solution:
    def ${sig.function}(${pyArgs(params)}) -> ${pyType(sig.returns)}:

`;
    }
    const doc = params.map(([n, t]) => ` * @param {${jsType(t)}} ${n}`).join('\n');
    const ret = sig.returns === 'void'
      ? ` * @return {void} Do not return anything, modify ${sig.output} in-place instead.`
      : ` * @return {${jsType(sig.returns)}}`;
    return `${head}/**
${doc}
${ret}
 */
var ${sig.function} = function(${params.map(p => p[0]).join(', ')}) {

};
`;
  }

  // 设计类：和 LeetCode 一样，C++ / Python 写成类，JavaScript 写成构造函数加原型方法
  function designTemplate(lang, d) {
    if (lang === 'cpp') {
      const methods = d.methods.map(m => `    ${cppType(m.returns)} ${m.name}(${cppArgs(m.params)}) {

    }`);
      return `class ${d.class} {
public:
    ${d.class}(${cppArgs(d.init)}) {

    }

${methods.join('\n\n')}
};
`;
    }
    if (lang === 'python') {
      const methods = d.methods.map(m => `    def ${m.name}(${pyArgs(m.params)}) -> ${pyType(m.returns)}:

`);
      return `class ${d.class}:

    def __init__(${pyArgs(d.init)}):


${methods.join('\n')}`;
    }
    const ctorDoc = d.init.length ? `/**\n${jsDoc(d.init)}\n */\n` : '';
    const methods = d.methods.map(m => `/**
${jsDoc(m.params, m.returns === 'void' ? undefined : m.returns)}${m.returns === 'void' ? (m.params.length ? '\n' : '') + ' * @return {void}' : ''}
 */
${d.class}.prototype.${m.name} = function(${m.params.map(p => p[0]).join(', ')}) {

};`);
    return `${ctorDoc}var ${d.class} = function(${d.init.map(p => p[0]).join(', ')}) {

};

${methods.join('\n\n')}
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

// 返回的是原结构里的某个节点：输出它在 ns 中的下标（nullptr 为 -1），不在 ns 里就输出错误信息
template <class N> void put_index(std::ostream& o, N* r, const std::vector<N*>& ns, const char* err) {
  if (!r) { o << -1; return; }
  for (size_t i = 0; i < ns.size(); i++) if (ns[i] == r) { o << i; return; }
  put(o, std::string(err));
}
template <class N> void put_val(std::ostream& o, N* r, const std::vector<N*>& ns, const char* err) {
  if (!r) { o << "null"; return; }
  for (N* n : ns) if (n == r) { o << r->val; return; }
  put(o, std::string(err));
}
// 带随机指针的链表：输出 [[val, random 的下标或 null], ...]，并检查没有用到原链表的节点
template <class N> void put_random(std::ostream& o, N* r, const std::vector<N*>& orig) {
  std::unordered_set<N*> old(orig.begin(), orig.end());
  std::unordered_map<N*, int> idx; std::vector<N*> ns;
  for (N* p = r; p && ns.size() < 200000; p = p->next) {
    if (old.count(p)) { put(o, std::string("返回的链表用到了原链表的节点")); return; }
    idx[p] = ns.size(); ns.push_back(p);
  }
  for (N* n : ns) if (n->random && !idx.count(n->random)) { put(o, std::string("random 指向了新链表以外的节点")); return; }
  o << '[';
  for (size_t i = 0; i < ns.size(); i++) {
    if (i) o << ',';
    o << '[' << ns[i]->val << ',';
    if (ns[i]->random) o << idx[ns[i]->random]; else o << "null";
    o << ']';
  }
  o << ']';
}
template <class N> std::vector<N*> chain(const std::vector<int>& v) {
  std::vector<N*> ns;
  for (int x : v) { ns.push_back(new N(x)); if (ns.size() > 1) ns[ns.size() - 2]->next = ns.back(); }
  return ns;
}
}
`;

  const CPP_NODE = 'struct Node { int val; Node* next; Node* random; Node(int _val) : val(_val), next(nullptr), random(nullptr) {} };\n';

  const cppDecl = (params, src) => params.map(([n, t], i) => `${cppType(t)} a${i}; lt_judge::get(${src(i)}, a${i});`);
  const CPP_T0 = 'auto t0 = std::chrono::steady_clock::now();';
  const CPP_MS = 'double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();';

  // 普通题目：每个参数一行
  function cppPlain(sig) {
    const args = sig.params.map((p, i) => `a${i}`).join(', ');
    const outIdx = sig.params.findIndex(p => p[0] === sig.output);
    return `    ${cppDecl(sig.params, i => 'lt_judge::line()').join('\n    ')}
    ${CPP_T0}
    ${sig.returns === 'void' ? '' : 'auto r = '}Solution().${sig.function}(${args});
    ${CPP_MS}
    lt_judge::put(lt_o, ${sig.returns === 'void' ? `a${outIdx}` : 'r'});`;
  }

  // 设计类：按操作名分派
  function cppDesign(sig) {
    const d = sig.design;
    const branch = (name, params, callExpr, returns) => `if (op == "${name}") { ${cppDecl(params, i => `a.a[${i}]`).join(' ')} ${
      returns === 'void' ? `${callExpr}; lt_o << "null";` : `lt_judge::put(lt_o, ${callExpr});`} }`;
    const args = params => params.map((p, i) => `a${i}`).join(', ');
    const branches = [branch(d.class, d.init, `obj = new ${d.class}(${args(d.init)})`, 'void')]
      .concat(d.methods.map(m => branch(m.name, m.params, `obj->${m.name}(${args(m.params)})`, m.returns)));
    return `    lt_judge::J ops = lt_judge::line(), argv = lt_judge::line();
    ${CPP_T0}
    lt_o << '[';
    ${d.class}* obj = nullptr;
    for (size_t i = 0; i < ops.a.size(); i++) {
      if (i) lt_o << ',';
      const std::string& op = ops.a[i].s;
      const lt_judge::J& a = i < argv.a.size() ? argv.a[i] : lt_judge::J();
      ${branches.join('\n      else ')}
      else lt_o << "null";
    }
    lt_o << ']';
    ${CPP_MS}`;
  }

  // 特殊输入：先按约定搭出链表 / 树
  function cppSpecial(sig) {
    const call = args => `${CPP_T0}
    auto r = Solution().${sig.function}(${args});
    ${CPP_MS}`;
    if (sig.input === 'cycle') return `    std::vector<int> v; lt_judge::get(lt_judge::line(), v);
    int pos; lt_judge::get(lt_judge::line(), pos);
    auto ns = lt_judge::chain<ListNode>(v);
    if (!ns.empty() && pos >= 0 && pos < (int)ns.size()) ns.back()->next = ns[pos];
    ListNode* head = ns.empty() ? nullptr : ns[0];
    ${call('head')}
    ${sig.returns === 'bool' ? 'lt_judge::put(lt_o, r);' : 'lt_judge::put_index(lt_o, r, ns, "返回的节点不在原链表中");'}`;
    if (sig.input === 'intersect') return `    std::vector<int> va, vb; int sa, sb;
    lt_judge::get(lt_judge::line(), va); lt_judge::get(lt_judge::line(), vb);
    lt_judge::get(lt_judge::line(), sa); lt_judge::get(lt_judge::line(), sb);
    auto na = lt_judge::chain<ListNode>(va);
    auto nb = lt_judge::chain<ListNode>(std::vector<int>(vb.begin(), vb.begin() + std::min<size_t>(std::max(sb, 0), vb.size())));
    ListNode* shared = sa >= 0 && sa < (int)na.size() ? na[sa] : nullptr;
    if (!nb.empty()) nb.back()->next = shared;
    ListNode* headA = na.empty() ? nullptr : na[0];
    ListNode* headB = nb.empty() ? shared : nb[0];
    ${call('headA, headB')}
    lt_judge::put_index(lt_o, r, na, "返回的节点不在 listA 中");`;
    if (sig.input === 'random') return `    lt_judge::J j = lt_judge::line();
    std::vector<int> v;
    for (auto& x : j.a) { int val; lt_judge::get(x.a[0], val); v.push_back(val); }
    auto ns = lt_judge::chain<Node>(v);
    for (size_t i = 0; i < ns.size(); i++)
      if (j.a[i].a.size() > 1 && j.a[i].a[1].t != 'n') { int k; lt_judge::get(j.a[i].a[1], k); ns[i]->random = ns[k]; }
    Node* head = ns.empty() ? nullptr : ns[0];
    ${call('head')}
    lt_judge::put_random(lt_o, r, ns);`;
    return `    TreeNode* root; lt_judge::get(lt_judge::line(), root);
    int pv, qv; lt_judge::get(lt_judge::line(), pv); lt_judge::get(lt_judge::line(), qv);
    std::vector<TreeNode*> ns;
    if (root) ns.push_back(root);
    for (size_t i = 0; i < ns.size(); i++) { if (ns[i]->left) ns.push_back(ns[i]->left); if (ns[i]->right) ns.push_back(ns[i]->right); }
    TreeNode *p = nullptr, *q = nullptr;
    for (TreeNode* n : ns) { if (n->val == pv) p = n; if (n->val == qv) q = n; }
    ${call('root, p, q')}
    lt_judge::put_val(lt_o, r, ns, "返回的节点不在原树中");`;
  }

  function cppProgram(sig, code) {
    const body = sig.design ? cppDesign(sig) : sig.input ? cppSpecial(sig) : cppPlain(sig);
    return `${CPP_PRELUDE}${uses(sig, 'Node') ? CPP_NODE : ''}#line 1 "solution.cpp"
${code}
#line 1 "lt_judge.cpp"
${CPP_DRIVER}
int main() {
  std::string head; std::getline(std::cin, head);
  int T = atoi(head.c_str());
  for (int t = 0; t < T; t++) {
    std::ostringstream lt_o;
${body}
    std::cout << std::flush << "\\036RES " << ms << " " << lt_o.str() << std::endl;
  }
  return 0;
}
`;
  }

  /* ---------- Python：驱动是固定的，签名作为参数传进去 ---------- */

  // 用户代码之前执行的内容：常用模块和 ListNode / TreeNode。补全（Jedi）也把它拼在代码前面，
  // 字段带类型注解，Jedi 才知道 head.next 还是 ListNode
  const PY_PRELUDE = `from typing import *
import collections, heapq, bisect, itertools, functools, math, string, re, random, operator
from collections import *
from heapq import *
from bisect import *
from itertools import *
from functools import *
inf = float('inf')
class ListNode:
    def __init__(self, val=0, next=None):
        self.val: int = val
        self.next: Optional['ListNode'] = next
class TreeNode:
    def __init__(self, val=0, left=None, right=None):
        self.val: int = val
        self.left: Optional['TreeNode'] = left
        self.right: Optional['TreeNode'] = right
class Node:
    def __init__(self, x=0, next=None, random=None):
        self.val: int = int(x)
        self.next: Optional['Node'] = next
        self.random: Optional['Node'] = random
`;

  const PY_DRIVER = `
import sys, json, time, traceback

_LT_PRELUDE = '''${PY_PRELUDE}'''

def _lt_tb(e):
    frames = [f for f in traceback.extract_tb(e.__traceback__) if f.filename == 'solution.py']
    lines = ['Line %d in %s' % (f.lineno, f.name) for f in frames[-3:]]
    lines.append('%s: %s' % (type(e).__name__, e))
    return {'msg': '\\n'.join(lines), 'line': frames[-1].lineno if frames else 0}

def _lt_judge(code, sig_json, inp):
    sig = json.loads(sig_json)
    ns = {'__name__': '__main__'}
    exec(_LT_PRELUDE, ns)
    ListNode, TreeNode, Node = ns['ListNode'], ns['TreeNode'], ns['Node']

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
    design = sig.get('design')
    main = design['class'] if design else 'Solution'
    if main not in ns:
        print('\\x1eERR ' + json.dumps({'msg': '没有找到 class ' + main, 'line': 0}, ensure_ascii=False))
        return

    def chain(vals, cls=ListNode):
        nodes = [cls(x) for x in vals]
        for a, b in zip(nodes, nodes[1:]):
            a.next = b
        return nodes

    def index_of(r, nodes, err):
        if r is None:
            return -1
        for i, n in enumerate(nodes):
            if n is r:
                return i
        return err

    # 设计类：按操作名依次调用，构造和 void 方法的结果为 None
    def run_design(ops, argv):
        methods = {m['name']: m for m in design['methods']}
        obj, out = None, []
        t0 = time.perf_counter()
        for op, a in zip(ops, argv):
            if op == main:
                obj = ns[main](*[de(x, p[1]) for x, p in zip(a, design['init'])])
                out.append(None)
            else:
                m = methods[op]
                r = getattr(obj, op)(*[de(x, p[1]) for x, p in zip(a, m['params'])])
                out.append(None if m['returns'] == 'void' else ser(r, m['returns']))
        return (time.perf_counter() - t0) * 1000, out

    # 特殊输入：按约定搭出链表 / 树，调用之后把返回的节点换成下标或值
    def run_special(kind, raw):
        fn = getattr(ns['Solution'](), sig['function'])
        if kind == 'cycle':
            nodes = chain(raw[0])
            if nodes and 0 <= raw[1] < len(nodes):
                nodes[-1].next = nodes[raw[1]]
            args = [nodes[0] if nodes else None]
            conv = lambda r: r if sig['returns'] == 'bool' else index_of(r, nodes, '返回的节点不在原链表中')
        elif kind == 'intersect':
            va, vb, sa, sb = raw
            nodes, nb = chain(va), chain(vb[:max(sb, 0)])
            shared = nodes[sa] if 0 <= sa < len(nodes) else None
            if nb:
                nb[-1].next = shared
            args = [nodes[0] if nodes else None, nb[0] if nb else shared]
            conv = lambda r: index_of(r, nodes, '返回的节点不在 listA 中')
        elif kind == 'random':
            nodes = chain([p[0] for p in raw[0]], Node)
            for n, p in zip(nodes, raw[0]):
                n.random = nodes[p[1]] if len(p) > 1 and p[1] is not None else None
            args = [nodes[0] if nodes else None]

            def conv(r):
                old = {id(n) for n in nodes}
                got, idx = [], {}
                while r is not None and len(got) < 200000:
                    if id(r) in old:
                        return '返回的链表用到了原链表的节点'
                    idx[id(r)] = len(got)
                    got.append(r)
                    r = r.next
                out = []
                for n in got:
                    if n.random is not None and id(n.random) not in idx:
                        return 'random 指向了新链表以外的节点'
                    out.append([n.val, None if n.random is None else idx[id(n.random)]])
                return out
        else:
            root = de(raw[0], 'TreeNode')
            nodes = [root] if root else []
            for n in nodes:
                nodes += [c for c in (n.left, n.right) if c]
            find = lambda v: next((n for n in nodes if n.val == v), None)
            args = [root, find(raw[1]), find(raw[2])]
            tree = {id(n) for n in nodes}
            conv = lambda r: None if r is None else r.val if id(r) in tree else '返回的节点不在原树中'
        t0 = time.perf_counter()
        r = fn(*args)
        return (time.perf_counter() - t0) * 1000, conv(r)

    def run_plain(raw):
        args = [de(v, p[1]) for v, p in zip(raw, sig['params'])]
        fn = getattr(ns['Solution'](), sig['function'])
        t0 = time.perf_counter()
        r = fn(*args)
        ms = (time.perf_counter() - t0) * 1000
        if sig.get('output'):
            i = [p[0] for p in sig['params']].index(sig['output'])
            return ms, ser(args[i], sig['params'][i][1])
        return ms, ser(r, sig['returns'])

    sys.setrecursionlimit(20000)
    lines = inp.split('\\n')
    k = 1
    for _ in range(int(lines[0])):
        raw = []
        for name, t in sig['params']:
            raw.append(json.loads(lines[k])); k += 1
        try:
            if design:
                ms, res = run_design(*raw)
            elif sig.get('input'):
                ms, res = run_special(sig['input'], raw)
            else:
                ms, res = run_plain(raw)
            out = json.dumps(res, separators=(',', ':'), ensure_ascii=False)
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
    G.Node = G._Node = function Node(val, next, random) { this.val = val === undefined ? 0 : val; this.next = next === undefined ? null : next; this.random = random === undefined ? null : random; };
    // 在同一次 eval 里取出函数（类）：用 class / let / const 声明的名字不会挂到全局对象上
    let fn;
    try {
      const name = sig.function;
      fn = g(`${code}\n;(typeof ${name} === 'undefined' ? undefined : ${name})\n//# sourceURL=solution.js`);
    } catch (e) {
      write((e instanceof SyntaxError ? '\x1eCE ' : '\x1eERR ') + JSON.stringify(errInfo(e)) + '\n');
      return;
    }
    if (typeof fn !== 'function') {
      write('\x1eERR ' + JSON.stringify({ msg: `没有找到${sig.design ? '类' : '函数'} ${sig.function}`, line: 0 }) + '\n');
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
    const chain = (vals, C = G.ListNode) => {
      const nodes = vals.map(x => new C(x));
      nodes.forEach((n, i) => { if (i) nodes[i - 1].next = n; });
      return nodes;
    };
    const indexOf = (r, nodes, err) => (r == null ? -1 : nodes.includes(r) ? nodes.indexOf(r) : err);

    // 设计类：按操作名依次调用，构造和 void 方法的结果为 null
    function runDesign([ops, argv]) {
      const d = sig.design, methods = new Map(d.methods.map(m => [m.name, m]));
      let obj = null;
      const out = [];
      const t0 = now();
      ops.forEach((op, i) => {
        const a = argv[i] || [];
        if (op === d.class) {
          obj = new fn(...d.init.map((p, j) => de(a[j], p[1])));
          out.push(null);
        } else {
          const m = methods.get(op);
          if (typeof obj[op] !== 'function') throw new TypeError(`${d.class} 没有方法 ${op}`);
          const r = obj[op](...m.params.map((p, j) => de(a[j], p[1])));
          out.push(m.returns === 'void' ? null : ser(r, m.returns));
        }
      });
      return [now() - t0, out];
    }

    // 特殊输入：按约定搭出链表 / 树，调用之后把返回的节点换成下标或值
    function runSpecial(raw) {
      let args, conv;
      if (sig.input === 'cycle') {
        const nodes = chain(raw[0]);
        if (nodes.length && raw[1] >= 0 && raw[1] < nodes.length) nodes[nodes.length - 1].next = nodes[raw[1]];
        args = [nodes[0] || null];
        conv = r => (sig.returns === 'bool' ? r : indexOf(r, nodes, '返回的节点不在原链表中'));
      } else if (sig.input === 'intersect') {
        const [va, vb, sa, sb] = raw;
        const nodes = chain(va), nb = chain(vb.slice(0, Math.max(sb, 0)));
        const shared = nodes[sa] || null;
        if (nb.length) nb[nb.length - 1].next = shared;
        args = [nodes[0] || null, nb.length ? nb[0] : shared];
        conv = r => indexOf(r, nodes, '返回的节点不在 listA 中');
      } else if (sig.input === 'random') {
        const nodes = chain(raw[0].map(p => p[0]), G.Node);
        raw[0].forEach((p, i) => { nodes[i].random = p[1] == null ? null : nodes[p[1]]; });
        args = [nodes[0] || null];
        conv = r => {
          const old = new Set(nodes), idx = new Map(), got = [];
          for (; r && got.length < 200000; r = r.next) {
            if (old.has(r)) return '返回的链表用到了原链表的节点';
            idx.set(r, got.length);
            got.push(r);
          }
          if (got.some(n => n.random && !idx.has(n.random))) return 'random 指向了新链表以外的节点';
          return got.map(n => [n.val, n.random ? idx.get(n.random) : null]);
        };
      } else {
        const root = de(raw[0], 'TreeNode'), nodes = root ? [root] : [];
        for (let i = 0; i < nodes.length; i++) [nodes[i].left, nodes[i].right].forEach(c => { if (c) nodes.push(c); });
        const find = v => nodes.find(n => n.val === v) || null;
        args = [root, find(raw[1]), find(raw[2])];
        conv = r => (r == null ? null : nodes.includes(r) ? r.val : '返回的节点不在原树中');
      }
      const t0 = now();
      const r = fn(...args);
      return [now() - t0, conv(r)];
    }

    function runPlain(raw) {
      const args = raw.map((v, i) => de(v, sig.params[i][1]));
      const outIdx = sig.params.findIndex(p => p[0] === sig.output);
      const t0 = now();
      const r = fn(...args);
      const ms = now() - t0;
      return [ms, sig.returns === 'void' ? ser(args[outIdx], sig.params[outIdx][1]) : ser(r, sig.returns)];
    }

    const lines = input.split('\n');
    let k = 1;
    for (let t = +lines[0]; t > 0; t--) {
      const raw = sig.params.map(() => JSON.parse(lines[k++]));
      try {
        const [ms, r] = sig.design ? runDesign(raw) : sig.input ? runSpecial(raw) : runPlain(raw);
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

  return { LANGS, parseType, signature, template, cppProgram, CLANG, CPP_FLAGS, cppFiles, PY_PRELUDE, PY_DRIVER, runJs, fmtLog, encodeInput, parseOutput, compare, evaluate };
});
