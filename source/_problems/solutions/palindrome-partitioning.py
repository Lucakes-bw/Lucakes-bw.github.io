class Solution:
    def partition(self, s: str) -> List[List[str]]:
        @cache
        def go(i):   # s[i:] 的所有分割方案
            if i == len(s):
                return [[]]
            return [[s[i:j]] + rest for j in range(i + 1, len(s) + 1) if s[i:j] == s[i:j][::-1] for rest in go(j)]
        return go(0)
