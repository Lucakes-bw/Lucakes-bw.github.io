# 把 bpftrace 输出的 @us[用户栈, 内核栈]: 微秒 折叠成火焰图脚本需要的格式：一行一个调用栈，从外到内，用分号连接
import re, sys
text = open(sys.argv[1]).read()
for m in re.finditer(r"@us\[(.*?)\]: (\d+)", text, re.S):
    parts = m.group(1).split("\n,")
    frames = []
    for p in parts:
        fs = [re.sub(r"\+\d+$", "", f.strip()) for f in p.strip().strip(",").split("\n") if f.strip()]
        frames.append(list(reversed(fs)))
    user, kern = frames[0], frames[1] if len(frames) > 1 else []
    print(";".join(user + [f + "_[k]" for f in kern]), m.group(2))
