class Solution:
    def longestPalindrome(self, s: str) -> str:
        n = len(s)
        pal = [[False] * n for _ in range(n)]   # pal[i][j]：s[i..j] 是回文
        start, best = 0, 1
        for i in range(n - 1, -1, -1):
            for j in range(i, n):
                if s[i] == s[j] and (j - i < 2 or pal[i + 1][j - 1]):
                    pal[i][j] = True
                    if j - i + 1 > best:
                        start, best = i, j - i + 1
        return s[start:start + best]
