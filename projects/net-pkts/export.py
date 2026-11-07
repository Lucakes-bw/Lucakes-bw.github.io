#!/usr/bin/env python3
"""把抓到的 pcap 中挑出来的几个包导出为 source/js/viz/net-pkts.js，供文章中的「逐字节拆包」组件使用。

用法（在仓库根目录）：python3 projects/net-pkts/export.py
"""
import json
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', '..', 'source', 'js', 'viz', 'net-pkts.js')

# 名字: (pcap 文件, 帧号从 1 开始, 标题, 说明)
PICK = {
    'dns-query': ('dns.pcap', 1, 'DNS 查询：dig @1.1.1.1 example.com', '一个 UDP 数据报，里面是 DNS 查询。'),
    'dns-answer': ('dns.pcap', 2, 'DNS 响应：1.1.1.1 的回答', '同一个查询 ID，带回两条 A 记录。'),
    'tcp-syn': ('http.pcap', 1, 'TCP 三次握手的第一个报文：SYN', 'curl 访问 http://example.com/ 时发出的第一个包。'),
    'tcp-synack': ('http.pcap', 2, 'TCP 三次握手的第二个报文：SYN+ACK', 'example.com 的回复。'),
    'http-get': ('http.pcap', 4, 'HTTP 请求：GET /', '三次握手之后的第一个数据报文。'),
    'tls-client-hello': ('tls.pcap', 4, 'TLS 1.3 的 Client Hello', 'curl 访问 https://example.com/，握手的第一条消息。'),
}


def frames(path):
    with open(path, 'rb') as f:
        data = f.read()
    magic = struct.unpack('<I', data[:4])[0]
    end = '<' if magic in (0xa1b2c3d4, 0xa1b23c4d) else '>'
    off = 24
    while off + 16 <= len(data):
        ts_sec, ts_frac, incl, _orig = struct.unpack(end + 'IIII', data[off:off + 16])
        off += 16
        yield ts_sec + ts_frac / 1e6, data[off:off + incl]
        off += incl


def main():
    out = {}
    for name, (fn, num, title, note) in PICK.items():
        fs = list(frames(os.path.join(HERE, fn)))
        _ts, raw = fs[num - 1]
        out[name] = {'title': title, 'note': note, 'hex': raw.hex()}
    with open(OUT, 'w') as f:
        f.write('/* 由 projects/net-pkts/export.py 从真实抓包生成，不要手改 */\n')
        f.write('window.PKTS = Object.assign(window.PKTS || {}, ')
        f.write(json.dumps(out, ensure_ascii=False, indent=1))
        f.write(');\n')
    print('写入', os.path.relpath(OUT), '共', len(out), '个包')


if __name__ == '__main__':
    main()
