# net-pkts：文章中逐字节拆开的真实抓包

网络专栏里的「逐字节拆包」组件（`themes/lucky-term/source/js/pktview.js`）用到的包都来自这里的三个 pcap，2026-11-07 在 Docker 的 Ubuntu 24.04 容器（172.17.0.3，网关 172.17.0.1）中用 tcpdump 4.99 抓取：

```bash
tcpdump -i eth0 -w dns.pcap -U "udp port 53" &
dig +noedns @1.1.1.1 example.com A        # dns.pcap 的第 1、2 个包
tcpdump -i eth0 -w http.pcap -U "tcp port 80" &
curl -s -o /dev/null http://example.com/   # http.pcap，13 个包
tcpdump -i eth0 -w tls.pcap -U "tcp port 443" &
curl -s -o /dev/null https://example.com/  # tls.pcap，第 4 个包是 Client Hello
```

`export.py` 中的 `PICK` 决定导出哪些包，运行后生成 `source/js/viz/net-pkts.js`：

```bash
python3 projects/net-pkts/export.py
```

组件中的解析结果和 tshark 4.2 的解析逐字段核对过（偏移、长度、值）。
