class Solution:
    def findAnagrams(self, s: str, p: str) -> List[int]:
        m = len(p)
        need, win, res = Counter(p), Counter(), []
        for i, c in enumerate(s):
            win[c] += 1
            if i >= m:
                win[s[i - m]] -= 1
                if win[s[i - m]] == 0:
                    del win[s[i - m]]
            if win == need:
                res.append(i - m + 1)
        return res
