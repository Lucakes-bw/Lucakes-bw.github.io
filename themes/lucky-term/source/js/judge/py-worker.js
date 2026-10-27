/**
 * Python Worker（常驻）：Pyodide（WebAssembly 版 CPython），首次加载约 10MB；补全用 Jedi，另外约 1.5MB
 *
 * 收到  { type: 'preload' | 'jedi', prelude }        加载 Pyodide，再在后台加载 Jedi
 *       { type: 'run', driver, code, sig, input }   运行判题
 *       { type: 'complete' | 'signatures', id, prelude, code, line, col }   补全 / 参数提示（line、col 从 1 开始）
 * 发出  { type: 'ready' } 加载完成   { type: 'jedi-ready' } 补全可用
 *       { type: 'start' } 开始执行（页面从这里开始计时）
 *       { type: 'out', text }   { type: 'done', stderr, error? }
 *       { type: 'completions' | 'signatures', id, items }
 */
const PYODIDE = 'https://cdn.jsdelivr.net/pyodide/v0.27.7/full/';
importScripts(PYODIDE + 'pyodide.js');

let py = null;
let jedi = null;
let driverLoaded = false;
let onOut = () => {}, onErr = () => {};

function init() {
  py = py || loadPyodide({ indexURL: PYODIDE }).then(p => {
    const dec = new TextDecoder(), decErr = new TextDecoder();
    p.setStdout({ write: buf => { onOut(dec.decode(buf, { stream: true })); return buf.length; } });
    p.setStderr({ write: buf => { onErr(decErr.decode(buf, { stream: true })); return buf.length; } });
    self.postMessage({ type: 'ready' });
    return p;
  });
  return py;
}

// 补全辅助函数：用户代码前面拼上 prelude，行号要加上 prelude 的行数
const JEDI_HELPER = `
import jedi, json

def _lt_script(prelude, code):
    return jedi.Script(prelude + code), prelude.count('\\n')

def _lt_complete(prelude, code, line, col):
    script, off = _lt_script(prelude, code)
    items = []
    for c in script.complete(line + off, col - 1)[:300]:
        items.append([c.name, c.type])
    return json.dumps(items)

def _lt_signatures(prelude, code, line, col):
    script, off = _lt_script(prelude, code)
    out = []
    for s in script.get_signatures(line + off, col - 1)[:3]:
        doc = s.docstring(raw=True) or ''
        out.append({'label': s.to_string(), 'params': [p.to_string() for p in s.params],
                    'index': s.index, 'doc': doc[:600]})
    return json.dumps(out)
`;

function initJedi(prelude) {
  jedi = jedi || init().then(async p => {
    await p.loadPackage('jedi', { messageCallback: () => {} });
    p.runPython(JEDI_HELPER);
    const j = { complete: p.globals.get('_lt_complete'), signatures: p.globals.get('_lt_signatures') };
    // 先补全一次：Jedi 第一次要解析 typing / collections 等的类型存根，要好几秒，放在后台做掉
    try { j.complete(prelude || '', 'x = deque()\nx.', 2, 3); } catch (e) { /* ignore */ }
    self.postMessage({ type: 'jedi-ready' });
    return j;
  });
  return jedi;
}

async function complete(data) {
  let items = [];
  try {
    const j = await initJedi(data.prelude);
    items = JSON.parse(j[data.type === 'complete' ? 'complete' : 'signatures'](data.prelude, data.code, data.line, data.col));
  } catch (e) { /* 补全失败就不给结果 */ }
  self.postMessage({ type: data.type === 'complete' ? 'completions' : 'signatures', id: data.id, items });
}

self.onmessage = async ({ data }) => {
  if (data.type === 'preload' || data.type === 'jedi') { await init(); initJedi(data.prelude); return; }
  if (data.type === 'complete' || data.type === 'signatures') { complete(data); return; }
  if (data.type !== 'run') return;
  let p;
  try {
    p = await init();
  } catch (e) {
    self.postMessage({ type: 'done', stderr: '', error: 'Python 运行环境加载失败：' + (e && e.message || e) });
    return;
  }
  let out = '', err = '';
  const flush = () => { if (out) { self.postMessage({ type: 'out', text: out }); out = ''; } };
  onOut = s => { out += s; if (s.includes('\x1e')) flush(); };
  onErr = s => { err += s; };
  self.postMessage({ type: 'start' });
  let error;
  try {
    if (!driverLoaded) { p.runPython(data.driver); driverLoaded = true; }
    const judge = p.globals.get('_lt_judge');
    judge(data.code, JSON.stringify(data.sig), data.input);
    judge.destroy();
  } catch (e) {
    error = String(e && e.message || e);
  }
  flush();
  self.postMessage({ type: 'done', stderr: err, error });
};
