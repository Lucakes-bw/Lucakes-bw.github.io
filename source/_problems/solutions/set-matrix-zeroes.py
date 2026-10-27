class Solution:
    def setZeroes(self, matrix: List[List[int]]) -> None:
        rows = {i for i, r in enumerate(matrix) if 0 in r}
        cols = {j for r in matrix for j, x in enumerate(r) if x == 0}
        for i, r in enumerate(matrix):
            for j in range(len(r)):
                if i in rows or j in cols:
                    r[j] = 0
