class Solution:
    def wordBreak(self, s: str, wordDict: List[str]) -> bool:
        words = set(wordDict)
        lens = sorted({len(w) for w in words})
        f = [True] + [False] * len(s)
        for i in range(1, len(s) + 1):
            f[i] = any(f[i - L] and s[i - L:i] in words for L in lens if L <= i)
        return f[-1]
