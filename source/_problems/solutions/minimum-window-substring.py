class Solution:
    def minWindow(self, s: str, t: str) -> str:
        need = Counter(t)
        missing, l, best = len(t), 0, (0, 0)
        for r, c in enumerate(s, 1):
            if need[c] > 0:
                missing -= 1
            need[c] -= 1
            if missing == 0:
                while need[s[l]] < 0:
                    need[s[l]] += 1
                    l += 1
                if best == (0, 0) or r - l < best[1] - best[0]:
                    best = (l, r)
                need[s[l]] += 1
                missing += 1
                l += 1
        return s[best[0]:best[1]]
