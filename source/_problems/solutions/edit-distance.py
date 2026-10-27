class Solution:
    def minDistance(self, word1: str, word2: str) -> int:
        f = list(range(len(word2) + 1))
        for i, a in enumerate(word1, 1):
            diag, f[0] = f[0], i
            for j, b in enumerate(word2, 1):
                diag, f[j] = f[j], diag if a == b else 1 + min(f[j], f[j - 1], diag)
        return f[-1]
