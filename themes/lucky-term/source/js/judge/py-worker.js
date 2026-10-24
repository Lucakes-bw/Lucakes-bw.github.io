/**
 * Python Worker（常驻）：Pyodide（WebAssembly 版 CPython），首次加载约 10MB
 *
 * 收到  { type: 'preload' }  或  { type: 'run', driver, code, sig, input }
 * 发出  { type: 'ready' } 加载完成   { type: 'start' } 开始执行（页面从这里开始计时）
 *       { type: 'out', text }   { type: 'done', stderr, error? }
 */
const PYODIDE = 'https://cdn.jsdelivr.net/pyodide/v0.27.7/full/';
importScripts(PYODIDE + 'pyodide.js');

let py = null;
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

self.onmessage = async ({ data }) => {
  if (data.type === 'preload') { await init(); return; }
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
