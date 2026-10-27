class Solution:
    def longestValidParentheses(self, s: str) -> int:
        # f[i]：以 s[i] 结尾的最长有效括号长度
        f, best = [0] * len(s), 0
        for i in range(1, len(s)):
            if s[i] != ')':
                continue
            if s[i - 1] == '(':
                f[i] = (f[i - 2] if i >= 2 else 0) + 2
            else:
                j = i - f[i - 1] - 1   # 和 s[i] 配对的位置
                if j >= 0 and s[j] == '(':
                    f[i] = f[i - 1] + 2 + (f[j - 1] if j >= 1 else 0)
            best = max(best, f[i])
        return best
