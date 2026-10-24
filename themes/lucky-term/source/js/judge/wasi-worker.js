/**
 * 运行编译好的 C++ 程序（每次运行一个新 Worker，超时由页面直接 terminate）
 *
 * 只实现了判题需要的一小部分 WASI preview1：标准输入输出、时钟、随机数、退出码，其余调用返回 ENOSYS。
 * 收到  { wasm, stdin }
 * 发出  { type: 'out', text }   标准输出（每跑完一个用例发一次，超时也能知道跑到了哪里）
 *       { type: 'done', stderr, exitCode, error? }
 */
const ESUCCESS = 0, EBADF = 8, ENOSYS = 52, ESPIPE = 70;
const LIMIT = 8 << 20;   // 输出上限 8MB，防止死循环打印把页面撑爆

class Exit { constructor(code) { this.code = code; } }

self.onmessage = async ({ data }) => {
  const stdin = new TextEncoder().encode(data.stdin);
  let inPos = 0;
  const dec = new TextDecoder();
  let out = '', err = '', total = 0;
  let memory;
  const view = () => new DataView(memory.buffer);
  const bytes = () => new Uint8Array(memory.buffer);
  const flush = () => { if (out) { self.postMessage({ type: 'out', text: out }); out = ''; } };

  const wasi = {
    fd_write(fd, iovs, n, nwritten) {
      if (fd !== 1 && fd !== 2) return EBADF;
      const v = view();
      let len = 0;
      for (let i = 0; i < n; i++) {
        const ptr = v.getUint32(iovs + i * 8, true), size = v.getUint32(iovs + i * 8 + 4, true);
        len += size;
        if (total > LIMIT) continue;
        total += size;
        const text = dec.decode(bytes().subarray(ptr, ptr + size), { stream: true });
        if (fd === 1) out += text; else err += text;
      }
      v.setUint32(nwritten, len, true);
      if (out.includes('\x1e') || out.length > 65536) flush();
      return ESUCCESS;
    },
    fd_read(fd, iovs, n, nread) {
      if (fd !== 0) return EBADF;
      const v = view();
      let len = 0;
      for (let i = 0; i < n && inPos < stdin.length; i++) {
        const ptr = v.getUint32(iovs + i * 8, true), size = v.getUint32(iovs + i * 8 + 4, true);
        const chunk = stdin.subarray(inPos, inPos + size);
        bytes().set(chunk, ptr);
        inPos += chunk.length;
        len += chunk.length;
      }
      v.setUint32(nread, len, true);
      return ESUCCESS;
    },
    fd_fdstat_get(fd, ptr) {
      if (fd > 2) return EBADF;
      const v = view();
      v.setUint8(ptr, 2);                         // character device
      v.setUint16(ptr + 2, 0, true);
      v.setBigUint64(ptr + 8, 0xffffffffffffffffn, true);
      v.setBigUint64(ptr + 16, 0xffffffffffffffffn, true);
      return ESUCCESS;
    },
    fd_fdstat_set_flags: () => ESUCCESS,
    fd_seek: fd => (fd <= 2 ? ESPIPE : EBADF),
    fd_close: () => ESUCCESS,
    fd_prestat_get: () => EBADF,               // 没有可访问的目录
    fd_prestat_dir_name: () => EBADF,
    args_sizes_get(argc, size) { view().setUint32(argc, 1, true); view().setUint32(size, 5, true); return ESUCCESS; },
    args_get(argv, buf) { view().setUint32(argv, buf, true); bytes().set([109, 97, 105, 110, 0], buf); return ESUCCESS; },
    environ_sizes_get(count, size) { view().setUint32(count, 0, true); view().setUint32(size, 0, true); return ESUCCESS; },
    environ_get: () => ESUCCESS,
    clock_res_get(id, res) { view().setBigUint64(res, 1000n, true); return ESUCCESS; },
    clock_time_get(id, precision, time) {
      const ns = id === 0 ? BigInt(Date.now()) * 1000000n : BigInt(Math.round(performance.now() * 1e6));
      view().setBigUint64(time, ns, true);
      return ESUCCESS;
    },
    random_get(buf, len) {
      for (let i = 0; i < len; i += 65536) crypto.getRandomValues(bytes().subarray(buf + i, buf + Math.min(len, i + 65536)));
      return ESUCCESS;
    },
    sched_yield: () => ESUCCESS,
    poll_oneoff: () => ENOSYS,
    proc_exit(code) { throw new Exit(code); }
  };
  const imports = { wasi_snapshot_preview1: new Proxy(wasi, { get: (t, k) => t[k] || (() => ENOSYS) }) };

  let exitCode = 0, error;
  try {
    const { instance } = await WebAssembly.instantiate(data.wasm, imports);
    memory = instance.exports.memory;
    instance.exports._start();
  } catch (e) {
    if (e instanceof Exit) exitCode = e.code;
    else error = String(e && e.message || e);
  }
  flush();
  self.postMessage({ type: 'done', stderr: err, exitCode, error });
};
