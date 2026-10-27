class Solution:
    def decodeString(self, s: str) -> str:
        i = 0
        def parse():   # 递归下降：解析到 ] 或结尾为止
            nonlocal i
            out, k = [], 0
            while i < len(s) and s[i] != ']':
                c = s[i]
                i += 1
                if c.isdigit():
                    k = k * 10 + int(c)
                elif c == '[':
                    inner = parse()
                    i += 1   # 跳过 ]
                    out.append(inner * k)
                    k = 0
                else:
                    out.append(c)
            return ''.join(out)
        return parse()
