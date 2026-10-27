class Solution:
    def longestCommonSubsequence(self, text1: str, text2: str) -> int:
        f = [0] * (len(text2) + 1)   # 滚动数组，diag 保存 f[i-1][j-1]
        for a in text1:
            diag = 0
            for j, b in enumerate(text2, 1):
                diag, f[j] = f[j], diag + 1 if a == b else max(f[j], f[j - 1])
        return f[-1]
