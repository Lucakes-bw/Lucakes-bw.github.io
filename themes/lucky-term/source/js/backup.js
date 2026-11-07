/**
 * 学习记录的备份与恢复：把本机 localStorage 里的学习数据导出成一个 JSON 文件，换浏览器或设备后再导入。
 * 由 main.js 在第一次打开「学习记录备份」对话框（layout/_partial/footer.ejs）时加载。
 *
 * 导出哪些键（都是 lt- 开头）：
 *   记录  lt-read（已读的文章）、lt-read-at（标记已读的时间）、lt-pb-state（做题状态）、lt-practice（跟练通过记录）、
 *         lt-review（每次通过的时间，复习页用）
 *   草稿  lt-pb-code:<题>:<语言>、lt-pb-cases:<题>（做题页的代码和自定义用例）、lt-snip:<页面>:<代码哈希>[:<练法>]（代码块草稿）
 *   偏好  lt-scheme、lt-ui、lt-pb-lang、lt-pb-layout
 *   lt-sw-dev、lt-focus、lt-pb-filter 这类调试开关和临时界面状态不导出。
 *
 * 导入是合并，不是覆盖：
 *   已读        取并集；已读时间两边都有时留更早的
 *   做题状态    通过 > 尝试过；都通过时留更早的通过时间，都只是尝试过时留更晚的
 *   跟练记录    按 页面 → 代码块 → 练法 合并，同一项留更早的通过时间
 *   复习记录    每项的通过时间取并集（最多留最近 20 次），小节标题本机没有才用备份里的
 *   草稿、偏好  本机没有才导入；本机已有的不动，免得盖掉正在写的代码
 *
 * 文件格式：{ app: 'lucky-blog', version: 1, exportedAt: 时间戳, data: { 键: localStorage 里的原始字符串 } }
 */
(function (global) {
  'use strict';

  const APP = 'lucky-blog';
  const VERSION = 1;
  const RECORD_KEYS = ['lt-read', 'lt-read-at', 'lt-pb-state', 'lt-practice', 'lt-review'];
  const PREF_KEYS = ['lt-scheme', 'lt-ui', 'lt-pb-lang', 'lt-pb-layout'];
  const DRAFT_PREFIXES = ['lt-pb-code:', 'lt-pb-cases:', 'lt-snip:'];
  const MAX_BYTES = 8 * 1024 * 1024;

  const isDraftKey = k => DRAFT_PREFIXES.some(p => k.startsWith(p));
  const isBackupKey = k => RECORD_KEYS.includes(k) || PREF_KEYS.includes(k) || isDraftKey(k);
  const parse = (s, d) => { try { const v = JSON.parse(s); return v == null ? d : v; } catch (e) { return d; } };
  const isObj = v => v && typeof v === 'object' && !Array.isArray(v);
  const minTime = (a, b) => (typeof a === 'number' && typeof b === 'number' ? Math.min(a, b) : (typeof a === 'number' ? a : b));

  /* ---------- 纯函数：本机数据和备份数据都用 { 键: 字符串 } 表示，方便在 Node 里测试 ---------- */

  // 本机数据 → 备份文件对象
  function pack(local, now) {
    const data = {};
    Object.keys(local).sort().forEach(k => { if (isBackupKey(k) && local[k] != null) data[k] = local[k]; });
    return { app: APP, version: VERSION, exportedAt: now, data };
  }

  // 检查导入的文件，返回 data；格式不对时抛出带中文说明的错误
  function unpack(text) {
    let obj;
    try { obj = JSON.parse(text); } catch (e) { throw new Error('文件不是有效的 JSON'); }
    if (!isObj(obj) || obj.app !== APP || !isObj(obj.data)) throw new Error('这不是本站导出的学习记录文件');
    if (typeof obj.version !== 'number' || obj.version > VERSION) throw new Error('文件来自更新版本的网站，请刷新页面后再试');
    const data = {};
    Object.keys(obj.data).forEach(k => { if (isBackupKey(k) && typeof obj.data[k] === 'string') data[k] = obj.data[k]; });
    return { data, exportedAt: obj.exportedAt };
  }

  function mergeRead(a, b) {
    const set = new Set([].concat(Array.isArray(a) ? a : [], Array.isArray(b) ? b : []).filter(u => typeof u === 'string'));
    return Array.from(set);
  }

  function mergeReadAt(a, b) {
    const out = Object.assign({}, isObj(a) ? a : {});
    if (isObj(b)) Object.keys(b).forEach(u => { if (typeof b[u] === 'number') out[u] = minTime(out[u], b[u]); });
    return out;
  }

  function betterPb(a, b) {
    if (!isObj(a)) return b;
    if (!isObj(b)) return a;
    const acA = a.status === 'ac', acB = b.status === 'ac';
    if (acA !== acB) return acA ? a : b;
    if (acA) return (b.at && (!a.at || b.at < a.at)) ? b : a;   // 都通过：留第一次通过
    return (b.at && (!a.at || b.at > a.at)) ? b : a;            // 都只是尝试过：留最近一次
  }

  function mergePbState(a, b) {
    const out = Object.assign({}, isObj(a) ? a : {});
    if (isObj(b)) Object.keys(b).forEach(slug => { const v = betterPb(out[slug], b[slug]); if (isObj(v)) out[slug] = v; });
    return out;
  }

  function mergePractice(a, b) {
    const out = parse(JSON.stringify(isObj(a) ? a : {}), {});
    if (!isObj(b)) return out;
    Object.keys(b).forEach(page => {
      if (!isObj(b[page])) return;
      const p = out[page] = isObj(out[page]) ? out[page] : {};
      Object.keys(b[page]).forEach(hash => {
        if (!isObj(b[page][hash])) return;
        const r = p[hash] = isObj(p[hash]) ? p[hash] : {};
        Object.keys(b[page][hash]).forEach(mode => {
          const t = b[page][hash][mode];
          if (typeof t === 'number') r[mode] = minTime(r[mode], t);
        });
      });
    });
    return out;
  }

  function mergeReview(a, b) {
    const out = parse(JSON.stringify(isObj(a) ? a : {}), {});
    if (!isObj(b)) return out;
    Object.keys(b).forEach(id => {
      if (!isObj(b[id])) return;
      const e = out[id] = isObj(out[id]) ? out[id] : {};
      const ts = new Set([].concat(Array.isArray(e.p) ? e.p : [], Array.isArray(b[id].p) ? b[id].p : []).filter(t => typeof t === 'number'));
      e.p = Array.from(ts).sort((x, y) => x - y).slice(-20);
      if (!e.t && typeof b[id].t === 'string') e.t = b[id].t;
    });
    return out;
  }

  // 统计一份数据里有多少条记录，用来显示「本机有…」和导入前后的变化
  function stats(local) {
    const read = parse(local['lt-read'], []);
    const pb = parse(local['lt-pb-state'], {});
    const pr = parse(local['lt-practice'], {});
    let practice = 0;
    if (isObj(pr)) Object.keys(pr).forEach(page => {
      if (isObj(pr[page])) Object.keys(pr[page]).forEach(h => { if (isObj(pr[page][h])) practice += Object.keys(pr[page][h]).length; });
    });
    return {
      read: Array.isArray(read) ? read.length : 0,
      ac: isObj(pb) ? Object.keys(pb).filter(k => pb[k] && pb[k].status === 'ac').length : 0,
      practice,
      drafts: Object.keys(local).filter(isDraftKey).length
    };
  }

  // 本机数据 + 备份数据 → 要写回本机的 { 键: 字符串 }（只含有变化的键），以及跳过的草稿数
  function merge(local, incoming) {
    const writes = {};
    let keptDrafts = 0;
    const put = (k, v) => { const s = JSON.stringify(v); if (s !== local[k]) writes[k] = s; };
    const L = k => parse(local[k], undefined), I = k => parse(incoming[k], undefined);

    if ('lt-read' in incoming) put('lt-read', mergeRead(L('lt-read'), I('lt-read')));
    if ('lt-read-at' in incoming) put('lt-read-at', mergeReadAt(L('lt-read-at'), I('lt-read-at')));
    if ('lt-pb-state' in incoming) put('lt-pb-state', mergePbState(L('lt-pb-state'), I('lt-pb-state')));
    if ('lt-practice' in incoming) put('lt-practice', mergePractice(L('lt-practice'), I('lt-practice')));
    if ('lt-review' in incoming) put('lt-review', mergeReview(L('lt-review'), I('lt-review')));

    Object.keys(incoming).forEach(k => {
      if (!isDraftKey(k) && !PREF_KEYS.includes(k)) return;
      if (local[k] == null) writes[k] = incoming[k];
      else if (local[k] !== incoming[k] && isDraftKey(k)) keptDrafts++;
    });
    return { writes, keptDrafts };
  }

  /* ---------- 浏览器部分 ---------- */

  function readLocal() {
    const out = {};
    try {
      for (let i = 0; i < localStorage.length; i++) {
        const k = localStorage.key(i);
        if (k && isBackupKey(k)) out[k] = localStorage.getItem(k);
      }
    } catch (e) { /* 隐私模式下读不到，当作空的 */ }
    return out;
  }

  const pad = n => String(n).padStart(2, '0');
  const fmtDate = t => { const d = new Date(t); return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}`; };
  const fmtTime = t => { const d = new Date(t); return `${fmtDate(t)} ${pad(d.getHours())}:${pad(d.getMinutes())}`; };
  const summary = s => `已读 ${s.read} 篇 · 通过 ${s.ac} 道题 · 跟练通过 ${s.practice} 次 · 草稿 ${s.drafts} 份`;

  function init(dialog) {
    const $ = s => dialog.querySelector(s);
    const statsEl = $('[data-bk-stats]'), msg = $('[data-bk-msg]'), file = $('[data-bk-file]');
    const say = (text, kind) => { msg.textContent = text; msg.dataset.kind = kind || ''; msg.hidden = !text; };

    const refresh = () => { statsEl.textContent = summary(stats(readLocal())); };
    dialog.addEventListener('lt-backup-open', () => { refresh(); say(''); });
    refresh();

    $('[data-bk-export]').addEventListener('click', () => {
      const local = readLocal();
      const s = stats(local);
      if (!s.read && !s.ac && !s.practice && !s.drafts) { say('本机还没有学习记录，没有可以导出的内容。', 'warn'); return; }
      const now = Date.now();
      const blob = new Blob([JSON.stringify(pack(local, now), null, 1)], { type: 'application/json' });
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = `lucky-blog-学习记录-${fmtDate(now)}.json`;
      document.body.appendChild(a);
      a.click();
      a.remove();
      setTimeout(() => URL.revokeObjectURL(a.href), 1000);
      say('已导出。在另一个浏览器里打开本站，点页脚的「学习记录备份」导入这个文件即可。', 'ok');
    });

    $('[data-bk-import]').addEventListener('click', () => file.click());
    file.addEventListener('change', () => {
      const f = file.files && file.files[0];
      file.value = '';
      if (!f) return;
      if (f.size > MAX_BYTES) { say('文件太大，不像是本站导出的学习记录。', 'err'); return; }
      const reader = new FileReader();
      reader.onerror = () => say('读取文件失败。', 'err');
      reader.onload = () => {
        let parsed;
        try { parsed = unpack(String(reader.result)); } catch (e) { say(e.message, 'err'); return; }
        const local = readLocal();
        const before = stats(local);
        const { writes, keptDrafts } = merge(local, parsed.data);
        const keys = Object.keys(writes);
        try {
          keys.forEach(k => localStorage.setItem(k, writes[k]));
        } catch (e) {
          say('写入失败：浏览器的本地存储满了或被禁用了（隐私模式下无法保存）。', 'err');
          return;
        }
        const after = stats(readLocal());
        const from = parsed.exportedAt ? `（${fmtTime(parsed.exportedAt)} 导出）` : '';
        const parts = [];
        if (after.read > before.read) parts.push(`已读 +${after.read - before.read} 篇`);
        if (after.ac > before.ac) parts.push(`通过 +${after.ac - before.ac} 道题`);
        if (after.practice > before.practice) parts.push(`跟练 +${after.practice - before.practice} 次`);
        if (after.drafts > before.drafts) parts.push(`草稿 +${after.drafts - before.drafts} 份`);
        let text = keys.length ? `导入完成${from}：${parts.length ? parts.join('，') : '更新了部分记录的时间和设置'}。` : `没有新内容${from}：备份里的记录本机都已经有了。`;
        if (keptDrafts) text += ` 有 ${keptDrafts} 份草稿本机也有、内容不同，保留了本机的。`;
        refresh();
        say(text, 'ok');
        if (keys.length) dialog.dataset.changed = '1';   // 关闭对话框时刷新页面，让进度条、热力图用上新数据
      };
      reader.readAsText(f);
    });
  }

  const api = { pack, unpack, merge, stats, init };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else global.LTBackup = api;
})(this);
