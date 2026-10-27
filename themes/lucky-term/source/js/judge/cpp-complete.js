/**
 * 做题页 C++ 补全：STL 名字、常用代码片段，以及按变量声明猜类型的成员补全（pos. → find / count / …）
 *
 * 不做真正的语义分析：在代码里找变量的声明，认出 vector<…> / unordered_map<…> 这类类型就列出它的成员。
 * 刷题代码的声明一般都很直白，这样能覆盖大部分情况；认不出来时不给成员补全。
 */
(function () {
  'use strict';

  // 成员：[名字, 说明]；说明以 ( 结尾的部分之前是返回值，带括号的是方法
  const SEQ = [
    ['size', 'size_t size()'], ['empty', 'bool empty()'], ['clear', 'void clear()'],
    ['begin', 'iterator begin()'], ['end', 'iterator end()'], ['rbegin', 'reverse_iterator rbegin()'], ['rend', 'reverse_iterator rend()'],
    ['front', 'T& front()'], ['back', 'T& back()'], ['insert', 'iterator insert(pos, x)'], ['erase', 'iterator erase(pos) / erase(first, last)'],
    ['swap', 'void swap(other)']
  ];
  const ASSOC = [
    ['size', 'size_t size()'], ['empty', 'bool empty()'], ['clear', 'void clear()'],
    ['begin', 'iterator begin()'], ['end', 'iterator end()'],
    ['insert', 'pair<iterator, bool> insert(x)'], ['emplace', 'pair<iterator, bool> emplace(args...)'],
    ['erase', 'size_t erase(key) / erase(it)'], ['find', 'iterator find(key)'], ['count', 'size_t count(key)'],
    ['contains', 'bool contains(key)  // C++20'], ['swap', 'void swap(other)']
  ];
  const ORDERED = [
    ['rbegin', 'reverse_iterator rbegin()'], ['rend', 'reverse_iterator rend()'],
    ['lower_bound', 'iterator lower_bound(key)  // 第一个 >= key'], ['upper_bound', 'iterator upper_bound(key)  // 第一个 > key'],
    ['equal_range', 'pair<iterator, iterator> equal_range(key)']
  ];
  const MAP = [['at', 'V& at(key)'], ['insert_or_assign', 'pair<iterator, bool> insert_or_assign(key, v)'], ['try_emplace', 'pair<iterator, bool> try_emplace(key, args...)']];
  const HASH = [['reserve', 'void reserve(n)'], ['bucket_count', 'size_t bucket_count()'], ['load_factor', 'float load_factor()']];
  const ADAPTER = [['size', 'size_t size()'], ['empty', 'bool empty()'], ['push', 'void push(x)'], ['emplace', 'void emplace(args...)'], ['pop', 'void pop()'], ['swap', 'void swap(other)']];

  const MEMBERS = {
    vector: SEQ.concat([
      ['push_back', 'void push_back(x)'], ['emplace_back', 'T& emplace_back(args...)'], ['pop_back', 'void pop_back()'],
      ['at', 'T& at(i)  // 越界会终止程序'], ['resize', 'void resize(n, value = T())'], ['reserve', 'void reserve(n)'],
      ['assign', 'void assign(n, value)'], ['capacity', 'size_t capacity()'], ['data', 'T* data()'], ['shrink_to_fit', 'void shrink_to_fit()']
    ]),
    string: SEQ.concat([
      ['length', 'size_t length()'], ['push_back', 'void push_back(c)'], ['pop_back', 'void pop_back()'], ['append', 'string& append(s)'],
      ['substr', 'string substr(pos, len = npos)'], ['find', 'size_t find(s, pos = 0)  // 找不到返回 npos'], ['rfind', 'size_t rfind(s)'],
      ['find_first_of', 'size_t find_first_of(chars)'], ['find_last_of', 'size_t find_last_of(chars)'],
      ['replace', 'string& replace(pos, len, s)'], ['compare', 'int compare(s)'], ['c_str', 'const char* c_str()'],
      ['at', 'char& at(i)'], ['resize', 'void resize(n, c = 0)'], ['reserve', 'void reserve(n)'],
      ['starts_with', 'bool starts_with(s)  // C++20'], ['ends_with', 'bool ends_with(s)  // C++20']
    ]),
    deque: SEQ.concat([
      ['push_back', 'void push_back(x)'], ['push_front', 'void push_front(x)'], ['pop_back', 'void pop_back()'], ['pop_front', 'void pop_front()'],
      ['emplace_back', 'T& emplace_back(args...)'], ['emplace_front', 'T& emplace_front(args...)'], ['at', 'T& at(i)'], ['resize', 'void resize(n)']
    ]),
    list: SEQ.concat([
      ['push_back', 'void push_back(x)'], ['push_front', 'void push_front(x)'], ['pop_back', 'void pop_back()'], ['pop_front', 'void pop_front()'],
      ['remove', 'void remove(x)'], ['reverse', 'void reverse()'], ['sort', 'void sort()'], ['unique', 'void unique()'],
      ['splice', 'void splice(pos, other, it)'], ['merge', 'void merge(other)']
    ]),
    array: SEQ.concat([['at', 'T& at(i)'], ['fill', 'void fill(x)'], ['data', 'T* data()']]),
    stack: ADAPTER.concat([['top', 'T& top()']]),
    queue: ADAPTER.concat([['front', 'T& front()'], ['back', 'T& back()']]),
    priority_queue: ADAPTER.concat([['top', 'const T& top()  // 默认是最大值']]),
    set: ASSOC.concat(ORDERED),
    multiset: ASSOC.concat(ORDERED),
    map: ASSOC.concat(ORDERED, MAP),
    multimap: ASSOC.concat(ORDERED),
    unordered_set: ASSOC.concat(HASH),
    unordered_multiset: ASSOC.concat(HASH),
    unordered_map: ASSOC.concat(HASH, MAP),
    unordered_multimap: ASSOC.concat(HASH),
    pair: [['first', 'T1 first'], ['second', 'T2 second']],
    bitset: [['set', 'bitset& set(pos)'], ['reset', 'bitset& reset(pos)'], ['flip', 'bitset& flip(pos)'], ['test', 'bool test(pos)'],
      ['count', 'size_t count()'], ['any', 'bool any()'], ['none', 'bool none()'], ['all', 'bool all()'], ['size', 'size_t size()'],
      ['to_string', 'string to_string()'], ['to_ulong', 'unsigned long to_ulong()']],
    ListNode: [['val', 'int val'], ['next', 'ListNode* next']],
    TreeNode: [['val', 'int val'], ['left', 'TreeNode* left'], ['right', 'TreeNode* right']]
  };
  const FIELDS = new Set(['first', 'second', 'val', 'next', 'left', 'right']);
  // 刷题时最常用的成员排在前面，其余按表里的顺序
  const COMMON = ['first', 'second', 'val', 'next', 'left', 'right', 'push_back', 'push', 'pop', 'top', 'front', 'back', 'pop_back',
    'find', 'count', 'insert', 'erase', 'size', 'empty', 'emplace_back', 'substr', 'lower_bound', 'upper_bound', 'begin', 'end'];
  const rank = (name, i) => { const c = COMMON.indexOf(name); return String(c < 0 ? 100 + i : c).padStart(3, '0'); };

  // 不在任何类型后面时的补全
  const TYPES = ['vector', 'string', 'deque', 'list', 'array', 'stack', 'queue', 'priority_queue', 'set', 'multiset', 'map', 'multimap',
    'unordered_set', 'unordered_multiset', 'unordered_map', 'unordered_multimap', 'pair', 'tuple', 'bitset', 'function', 'optional',
    'string_view', 'greater', 'less', 'numeric_limits', 'ListNode', 'TreeNode'];
  const FUNCS = [
    ['sort', 'sort(first, last, comp = less)'], ['stable_sort', 'stable_sort(first, last, comp)'], ['reverse', 'reverse(first, last)'],
    ['max', 'max(a, b) / max({a, b, c})'], ['min', 'min(a, b) / min({a, b, c})'], ['swap', 'swap(a, b)'], ['abs', 'abs(x)'],
    ['lower_bound', 'lower_bound(first, last, x)  // 第一个 >= x'], ['upper_bound', 'upper_bound(first, last, x)  // 第一个 > x'],
    ['binary_search', 'binary_search(first, last, x)'], ['accumulate', 'accumulate(first, last, init)  // 注意 init 的类型'],
    ['max_element', 'max_element(first, last)'], ['min_element', 'min_element(first, last)'], ['minmax_element', 'minmax_element(first, last)'],
    ['unique', 'unique(first, last)  // 配合 erase 去重'], ['fill', 'fill(first, last, x)'], ['iota', 'iota(first, last, start)'],
    ['count', 'count(first, last, x)'], ['count_if', 'count_if(first, last, pred)'], ['find', 'find(first, last, x)'], ['find_if', 'find_if(first, last, pred)'],
    ['next_permutation', 'next_permutation(first, last)'], ['prev_permutation', 'prev_permutation(first, last)'],
    ['partial_sum', 'partial_sum(first, last, out)'], ['nth_element', 'nth_element(first, nth, last)'], ['partition', 'partition(first, last, pred)'],
    ['all_of', 'all_of(first, last, pred)'], ['any_of', 'any_of(first, last, pred)'], ['none_of', 'none_of(first, last, pred)'],
    ['gcd', 'gcd(a, b)'], ['lcm', 'lcm(a, b)'], ['pow', 'pow(x, y)  // double'], ['sqrt', 'sqrt(x)'], ['log2', 'log2(x)'],
    ['to_string', 'to_string(x)'], ['stoi', 'stoi(s)'], ['stoll', 'stoll(s)'], ['stod', 'stod(s)'],
    ['isdigit', 'isdigit(c)'], ['isalpha', 'isalpha(c)'], ['isalnum', 'isalnum(c)'], ['islower', 'islower(c)'], ['isupper', 'isupper(c)'],
    ['tolower', 'tolower(c)'], ['toupper', 'toupper(c)'], ['make_pair', 'make_pair(a, b)'], ['make_tuple', 'make_tuple(args...)'], ['tie', 'tie(a, b) = …'],
    ['memset', 'memset(p, value, bytes)'], ['__builtin_popcount', '__builtin_popcount(x)  // 二进制里 1 的个数'],
    ['__builtin_clz', '__builtin_clz(x)  // 前导 0 的个数'], ['__builtin_ctz', '__builtin_ctz(x)  // 末尾 0 的个数']
  ];
  const CONSTS = ['INT_MAX', 'INT_MIN', 'LLONG_MAX', 'LLONG_MIN', 'UINT_MAX', 'nullptr', 'true', 'false', 'npos'];
  const KEYWORDS = ['int', 'long long', 'double', 'bool', 'char', 'void', 'auto', 'const', 'static', 'unsigned', 'return', 'if', 'else',
    'for', 'while', 'do', 'break', 'continue', 'switch', 'case', 'default', 'class', 'struct', 'public', 'private', 'new', 'delete',
    'sizeof', 'using', 'namespace', 'template', 'typename', 'this', 'operator', 'inline', 'constexpr', 'emplace'];
  const SNIPPETS = [
    ['fori', 'for (int ${1:i} = 0; ${1:i} < ${2:n}; ${1:i}++) {\n\t$0\n}', 'for 循环'],
    ['forr', 'for (int ${1:i} = ${2:n} - 1; ${1:i} >= 0; ${1:i}--) {\n\t$0\n}', '倒序 for 循环'],
    ['fore', 'for (auto& ${1:x} : ${2:nums}) {\n\t$0\n}', '范围 for'],
    ['all', '${1:v}.begin(), ${1:v}.end()', 'begin(), end()'],
    ['sortall', 'sort(${1:v}.begin(), ${1:v}.end());', '整个容器排序'],
    ['lambda', 'auto ${1:f} = [&](${2:int x}) {\n\t$0\n};', 'lambda'],
    ['dfs', 'function<void(int)> ${1:dfs} = [&](int ${2:u}) {\n\t$0\n};', '可以递归的 lambda'],
    ['minheap', 'priority_queue<${1:int}, vector<${1:int}>, greater<${1:int}>> ${2:pq};', '小根堆'],
    ['vvi', 'vector<vector<int>> ${1:g}(${2:n}, vector<int>(${3:m}${4:, 0}));', '二维数组'],
    ['dirs', 'int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};', '上下左右四个方向'],
    ['inf', 'const int INF = 0x3f3f3f3f;', '无穷大常量'],
    ['ll', 'using ll = long long;', 'long long 别名']
  ];
  const NOT_TYPES = new Set(['return', 'else', 'case', 'new', 'delete', 'throw', 'goto', 'using', 'typedef', 'co_return', 'sizeof', 'struct', 'class']);

  // ---------- 类型 ----------

  // "unordered_map<int, vector<int>>" → { base: 'unordered_map', args: ['int', 'vector<int>'] }
  function parseType(t) {
    t = t.replace(/\bstd::/g, '').replace(/\bconst\b/g, '').trim();
    const lt = t.indexOf('<');
    if (lt < 0) return { base: t.replace(/[\s&*]+$/, ''), args: [] };
    const args = [];
    let depth = 0, cur = '';
    for (const ch of t.slice(lt + 1, t.lastIndexOf('>'))) {
      if (ch === '<') depth++;
      if (ch === '>') depth--;
      if (ch === ',' && depth === 0) { args.push(cur.trim()); cur = ''; } else cur += ch;
    }
    if (cur.trim()) args.push(cur.trim());
    return { base: t.slice(0, lt).trim(), args };
  }
  const isMap = b => /map$/.test(b);
  // v[i] 的类型
  function indexType(t) {
    const { base, args } = parseType(t);
    if (base === 'string') return 'char';
    if (isMap(base)) return args[1] || '';
    return args[0] || '';
  }
  // for (auto x : v) 里 x 的类型
  function elemType(t) {
    const { base, args } = parseType(t);
    if (isMap(base)) return `pair<${args[0]}, ${args[1]}>`;
    if (base === 'string') return 'char';
    return args[0] || '';
  }

  // 在光标之前找变量 name 最近的一次声明，返回类型字符串（认不出来返回空串）
  function declType(text, name) {
    const id = name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    let best = { at: -1, type: () => '' };
    const take = (at, type) => { if (at > best.at) best = { at, type }; };
    let m;
    // 普通声明和函数参数：TYPE[&*] name [;,=({[)]。只认识的类型，避免把 a * name 当成声明
    const decl = new RegExp(`([A-Za-z_][\\w:]*(?:\\s*<[^;(){}]*>)?)\\s*[&*]*\\s*\\b${id}\\b\\s*(?=[;,=({)\\[])`, 'g');
    while ((m = decl.exec(text))) {
      const t = m[1].trim();
      if (!NOT_TYPES.has(t) && MEMBERS[parseType(t).base]) take(m.index, () => t);
    }
    // 范围 for：for (auto& name : expr)
    const rf = new RegExp(`for\\s*\\(\\s*(?:const\\s+)?auto\\s*&{0,2}\\s*${id}\\s*:\\s*([A-Za-z_]\\w*)`, 'g');
    while ((m = rf.exec(text))) {
      const at = m.index, src = m[1];
      take(at, () => { const t = declType(text.slice(0, at), src); return t ? elemType(t) : ''; });
    }
    // 迭代器：auto it = mp.find(…) / mp.begin() / lower_bound …
    const it = new RegExp(`auto\\s+${id}\\s*=\\s*([A-Za-z_]\\w*)\\s*\\.\\s*(?:find|begin|end|rbegin|rend|lower_bound|upper_bound)\\s*\\(`, 'g');
    while ((m = it.exec(text))) {
      const at = m.index, src = m[1];
      take(at, () => { const t = declType(text.slice(0, at), src); return t ? `iterator<${elemType(t)}>` : ''; });
    }
    return best.type();
  }

  // 光标前的表达式，比如 "g[i][j]." / "it->" / "head->next->"，返回它的类型
  function exprType(text, before) {
    const m = /([A-Za-z_]\w*)((?:\s*\[[^\[\]]*\])*)((?:\s*->\s*\w+)*)\s*(\.|->)\s*\w*$/.exec(before);
    if (!m) return null;
    let t = declType(text, m[1]);
    if (!t) return null;
    const idx = (m[2].match(/\[/g) || []).length;
    for (let i = 0; i < idx && t; i++) t = indexType(t);
    // head->next->：链表 / 树的字段还是同样的类型
    if (m[3]) {
      const base = parseType(t).base;
      if (base !== 'ListNode' && base !== 'TreeNode') return null;
    }
    let { base, args } = parseType(t);
    if (base === 'iterator') {
      if (m[4] !== '->') return null;
      ({ base, args } = parseType(args[0] || ''));
    }
    return MEMBERS[base] ? base : null;
  }

  // ---------- 注册 ----------

  function register(monaco) {
    const K = monaco.languages.CompletionItemKind;
    const SNIPPET = monaco.languages.CompletionItemInsertTextRule.InsertAsSnippet;

    monaco.languages.registerCompletionItemProvider('cpp', {
      triggerCharacters: ['.', '>'],
      provideCompletionItems(model, pos) {
        const word = model.getWordUntilPosition(pos);
        const range = { startLineNumber: pos.lineNumber, endLineNumber: pos.lineNumber, startColumn: word.startColumn, endColumn: word.endColumn };
        const before = model.getValueInRange({ startLineNumber: 1, startColumn: 1, endLineNumber: pos.lineNumber, endColumn: pos.column });
        const line = before.slice(before.lastIndexOf('\n') + 1);

        // 成员补全
        if (/(\.|->)\s*\w*$/.test(line)) {
          if (/[^-]>\s*\w*$/.test(line) && !/->\s*\w*$/.test(line)) return { suggestions: [] };   // 只是一个 >
          const type = exprType(before, line);
          if (!type) return { suggestions: [] };
          return {
            suggestions: MEMBERS[type].map(([name, doc], i) => {
              const field = FIELDS.has(name);
              return {
                label: { label: name, description: type },
                kind: field ? K.Field : K.Method,
                detail: doc,
                insertText: field ? name : `${name}($0)`,
                insertTextRules: field ? undefined : SNIPPET,
                sortText: rank(name, i),
                range
              };
            })
          };
        }
        if (/^\s*#\s*\w*$/.test(line)) return { suggestions: [] };

        // 普通补全：代码里出现过的名字、STL、片段、关键字
        const seen = new Set();
        const out = [];
        const add = item => { if (!seen.has(item.label.label || item.label)) { seen.add(item.label.label || item.label); out.push(Object.assign({ range }, item)); } };
        SNIPPETS.forEach(([name, body, doc]) => add({ label: { label: name, description: doc }, kind: K.Snippet, insertText: body, insertTextRules: SNIPPET, detail: doc, sortText: '1' }));
        const words = model.getValue().match(/[A-Za-z_]\w*/g) || [];
        words.forEach(w => { if (w !== word.word && w.length > 1) add({ label: w, kind: K.Text, insertText: w, sortText: '0' }); });
        TYPES.forEach(t => add({ label: { label: t, description: 'STL' }, kind: K.Class, insertText: t, sortText: '2' }));
        FUNCS.forEach(([name, doc]) => add({ label: { label: name, description: 'STL' }, kind: K.Function, detail: doc, insertText: `${name}($0)`, insertTextRules: SNIPPET, sortText: '2' }));
        CONSTS.forEach(c => add({ label: c, kind: K.Constant, insertText: c, sortText: '3' }));
        KEYWORDS.forEach(k => add({ label: k, kind: K.Keyword, insertText: k, sortText: '3' }));
        return { suggestions: out };
      }
    });
  }

  window.LTCppComplete = { register, _test: { declType, exprType, parseType } };
})();
