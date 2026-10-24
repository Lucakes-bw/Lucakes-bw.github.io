/**
 * JavaScript Worker（每次运行一个新 Worker，用户代码之间互不影响）
 *
 * 收到  { lang, code, sig, input }   lang 是 lang.js 的地址
 * 发出  { type: 'out', text }   { type: 'done', error? }
 */
self.onmessage = ({ data }) => {
  importScripts(data.lang);
  const J = self.LTJudge;
  let out = '';
  const write = s => {
    out += s;
    if (s.includes('\x1e') || out.length > 65536) { self.postMessage({ type: 'out', text: out }); out = ''; }
  };
  const log = (...args) => write(J.fmtLog(args) + '\n');
  ['log', 'info', 'warn', 'error', 'debug'].forEach(k => { console[k] = log; });
  let error;
  try {
    J.runJs(data.code, data.sig, data.input, write);
  } catch (e) {
    error = String(e && e.message || e);
  }
  if (out) self.postMessage({ type: 'out', text: out });
  self.postMessage({ type: 'done', error });
};
