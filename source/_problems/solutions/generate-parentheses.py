class Solution:
    def generateParenthesis(self, n: int) -> List[str]:
        # 有效串一定能写成 "(" + A + ")" + B，A 和 B 也是有效串
        f = [['']]
        for k in range(1, n + 1):
            f.append(['(' + a + ')' + b for i in range(k) for a in f[i] for b in f[k - 1 - i]])
        return f[n]
