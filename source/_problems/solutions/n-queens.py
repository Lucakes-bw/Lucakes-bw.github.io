class Solution:
    def solveNQueens(self, n: int) -> List[List[str]]:
        res, pos = [], []
        def go(r, cols, d1, d2):
            if r == n:
                res.append(['.' * c + 'Q' + '.' * (n - 1 - c) for c in pos])
                return
            for c in range(n):
                if c not in cols and r - c not in d1 and r + c not in d2:
                    pos.append(c)
                    go(r + 1, cols | {c}, d1 | {r - c}, d2 | {r + c})
                    pos.pop()
        go(0, set(), set(), set())
        return res
