/**
 * C++ 编译 Worker（module worker，常驻）：在浏览器里运行 Clang/LLD，把 C++ 编译成 WASI 的 wasm
 *
 * 编译器来自 npm 包 @yowasp/clang，从 jsDelivr 加载：首次约 23MB，之后走浏览器缓存。
 * 收到  { type: 'compile', id, pkg, args, files }  或  { type: 'preload', pkg }
 * 发出  { type: 'progress', done, total }
 *       { type: 'compiled', id, ok, wasm?, stderr }
 */
let clang = null;

function load(pkg) {
  clang = clang || import(`https://cdn.jsdelivr.net/npm/${pkg}/gen/bundle.js`).then(m => m.runClang);
  return clang;
}

let last = 0;
const progress = e => {
  const now = Date.now();
  if (now - last < 120 && e.doneLength < e.totalLength) return;
  last = now;
  self.postMessage({ type: 'progress', done: e.doneLength, total: e.totalLength });
};

async function run(pkg, args, files) {
  const runClang = await load(pkg);
  const dec = new TextDecoder();
  let stderr = '';
  const err = b => { if (b) stderr += dec.decode(b, { stream: true }); };
  try {
    const out = await runClang(args, files, { stdout: err, stderr: err, fetchProgress: progress });
    return { ok: true, out, stderr };
  } catch (e) {
    return { ok: false, stderr: stderr || String(e && e.message || e) };
  }
}

self.onmessage = async ({ data }) => {
  if (data.type === 'preload') {
    // 跑一次 --version，把编译器和系统头文件都下载、编译好
    const r = await run(data.pkg, ['clang++', '--version'], {});
    self.postMessage({ type: 'ready', ok: r.ok, stderr: r.ok ? '' : r.stderr });
  } else if (data.type === 'compile') {
    const r = await run(data.pkg, data.args, data.files);
    const wasm = r.ok && r.out && r.out['main.wasm'];
    self.postMessage({ type: 'compiled', id: data.id, ok: !!wasm, wasm, stderr: r.stderr }, wasm ? [wasm.buffer] : []);
  }
};
