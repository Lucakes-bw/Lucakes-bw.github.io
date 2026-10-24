#!/usr/bin/env node
/**
 * 检查题库 source/_problems/：
 *   1. front-matter 能解析成签名，每个用例的参数个数对得上
 *   2. C++ 模板加上判题驱动能编译（检查类型映射）
 *   3. source/_problems/solutions/<slug>.{cpp,py,js} 的参考解答能通过全部用例
 *
 * 用法：node tools/check-problems.mjs [slug ...]
 * C++ 用的是和网页里同一个编译器，需要先装上（不写进 package.json，免得部署时也下载）：
 *   npm i --no-save @yowasp/clang@22.0.0-git20542-10
 */
import { createRequire } from 'node:module';
import { spawnSync } from 'node:child_process';
import { WASI } from 'node:wasi';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const require = createRequire(import.meta.url);
const ROOT = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const LANG_JS = path.join(ROOT, 'themes/lucky-term/source/js/judge/lang.js');
const J = require(LANG_JS);
const yfm = require('hexo-front-matter');

const DIR = path.join(ROOT, 'source/_problems');
const only = process.argv.slice(2);
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'lt-judge-'));

let clang = null;
try {
  ({ runClang: clang } = await import('@yowasp/clang'));
} catch (e) {
  console.log(`\x1b[33m! 没有安装 C++ 编译器，跳过 C++：npm i --no-save ${J.CLANG.pkg}@${J.CLANG.version}\x1b[0m\n`);
}

async function compileCpp(source, extra = []) {
  let stderr = '';
  const dec = new TextDecoder();
  try {
    const out = await clang(['clang++', ...J.CPP_FLAGS, ...extra, 'main.cpp', '-o', 'main.wasm'], J.cppFiles(source),
      { stderr: b => { if (b) stderr += dec.decode(b, { stream: true }); }, stdout: () => {} });
    return { wasm: out && out['main.wasm'] };
  } catch (e) {
    return { error: stderr || String(e) };
  }
}

async function runCpp(sig, code, input) {
  const { wasm, error } = await compileCpp(J.cppProgram(sig, code));
  if (error) return { compileError: error };
  const inFile = path.join(tmp, 'in.txt'), outFile = path.join(tmp, 'out.txt'), errFile = path.join(tmp, 'err.txt');
  fs.writeFileSync(inFile, input);
  const fds = [fs.openSync(inFile, 'r'), fs.openSync(outFile, 'w'), fs.openSync(errFile, 'w')];
  const wasi = new WASI({ version: 'preview1', stdin: fds[0], stdout: fds[1], stderr: fds[2], returnOnExit: true });
  let runtimeError;
  try {
    const inst = await WebAssembly.instantiate(await WebAssembly.compile(wasm), { wasi_snapshot_preview1: wasi.wasiImport });
    wasi.start(inst);
  } catch (e) {
    runtimeError = String(e);
  }
  fds.forEach(fd => fs.closeSync(fd));
  return { stdout: fs.readFileSync(outFile, 'utf8'), stderr: fs.readFileSync(errFile, 'utf8'), runtimeError };
}

function runPython(sig, code, input) {
  const codeFile = path.join(tmp, 'solution.py');
  fs.writeFileSync(codeFile, code);
  const py = J.PY_DRIVER + '\n_lt_judge(open(sys.argv[1], encoding="utf-8").read(), sys.argv[2], sys.stdin.read())\n';
  const r = spawnSync('python3', ['-c', py, codeFile, JSON.stringify(sig)], { input, encoding: 'utf8', timeout: 20000 });
  return { stdout: r.stdout || '', stderr: r.stderr || '', timeout: r.error && r.error.code === 'ETIMEDOUT' };
}

function runJavascript(sig, code, input) {
  const codeFile = path.join(tmp, 'solution.js');
  fs.writeFileSync(codeFile, code);
  const js = `const J = require(${JSON.stringify(LANG_JS)});
const w = s => process.stdout.write(s);
console.log = console.info = console.warn = console.error = (...a) => w(J.fmtLog(a) + '\\n');
J.runJs(require('fs').readFileSync(process.argv[1], 'utf8'), JSON.parse(process.argv[2]), require('fs').readFileSync(0, 'utf8'), w);`;
  const r = spawnSync(process.execPath, ['-e', js, codeFile, JSON.stringify(sig)], { input, encoding: 'utf8', timeout: 20000 });
  return { stdout: r.stdout || '', stderr: r.stderr || '', timeout: r.error && r.error.code === 'ETIMEDOUT' };
}

const RUN = { cpp: runCpp, python: runPython, javascript: runJavascript };
const EXT = { cpp: 'cpp', python: 'py', javascript: 'js' };
const show = v => JSON.stringify(v);

let failed = 0;
const files = fs.readdirSync(DIR).filter(f => f.endsWith('.md') && f !== 'README.md')
  .map(f => [f, +(/^id:\s*(\d+)/m.exec(fs.readFileSync(path.join(DIR, f), 'utf8')) || [0, 0])[1]])
  .sort((a, b) => a[1] - b[1]).map(x => x[0]);
for (const f of files) {
  const slug = f.slice(0, -3);
  if (only.length && !only.includes(slug)) continue;
  const problems = [];
  const fm = yfm.parse(fs.readFileSync(path.join(DIR, f), 'utf8'));
  let sig;
  try {
    sig = J.signature(fm);
    const cases = [].concat(fm.examples || [], fm.tests || []);
    if (!fm.id || !fm.title || !fm.difficulty) problems.push('缺少 id / title / difficulty');
    if (!(fm.examples || []).length) problems.push('至少需要一个 examples');
    cases.forEach((c, i) => {
      if (!Array.isArray(c.in) || c.in.length !== sig.params.length) problems.push(`第 ${i + 1} 个用例的参数个数不对`);
      if (!('out' in c)) problems.push(`第 ${i + 1} 个用例缺少 out`);
    });
    fm._cases = cases;
  } catch (e) {
    problems.push(e.message);
  }
  console.log(`\x1b[1m${String(fm.id).padStart(3)} ${slug}\x1b[0m  ${fm.title}`);
  if (problems.length) {
    problems.forEach(p => console.log(`    \x1b[31m✗ ${p}\x1b[0m`));
    failed++;
    continue;
  }

  if (clang) {
    const { error } = await compileCpp(J.cppProgram(sig, J.template('cpp', sig)), ['-fsyntax-only', '-Wno-return-type']);
    // -fsyntax-only 不产出文件，runClang 可能因为拿不到 main.wasm 而报错；只看有没有 error:
    if (error && /error:/.test(error)) { console.log(`    \x1b[31m✗ C++ 模板编译失败\x1b[0m\n${error}`); failed++; }
  }

  for (const lang of Object.keys(RUN)) {
    const file = path.join(DIR, 'solutions', `${slug}.${EXT[lang]}`);
    if (!fs.existsSync(file)) { console.log(`    \x1b[2m- ${lang} 没有参考解答\x1b[0m`); continue; }
    if (lang === 'cpp' && !clang) continue;
    const cases = fm._cases;
    const t0 = Date.now();
    const res = J.evaluate(sig, cases, await RUN[lang](sig, fs.readFileSync(file, 'utf8'), J.encodeInput(cases)));
    const ms = Date.now() - t0;
    if (res.status === 'AC') {
      console.log(`    \x1b[32m✓ ${lang.padEnd(10)} ${cases.length}/${cases.length}\x1b[0m \x1b[2m${ms}ms\x1b[0m`);
      continue;
    }
    failed++;
    const i = res.cases.findIndex(c => c.status !== 'AC');
    console.log(`    \x1b[31m✗ ${lang.padEnd(10)} ${res.status}\x1b[0m`);
    if (res.compileError) console.log(res.compileError);
    else {
      const c = res.cases[i];
      console.log(`      用例 ${i + 1}：输入 ${cases[i].in.map(show).join(', ')}`);
      console.log(`      预期 ${show(cases[i].out)}  实际 ${show(c.got)}${c.error ? '  错误 ' + c.error : ''}`);
      if (c.stdout) console.log(`      stdout ${c.stdout}`);
    }
  }
}

fs.rmSync(tmp, { recursive: true, force: true });
console.log(failed ? `\n\x1b[31m${failed} 处问题\x1b[0m` : '\n\x1b[32m全部通过\x1b[0m');
process.exit(failed ? 1 : 0);
