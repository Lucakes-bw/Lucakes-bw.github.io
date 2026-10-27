class Solution:
    def spiralOrder(self, matrix: List[List[int]]) -> List[int]:
        m, n = len(matrix), len(matrix[0])
        seen = [[False] * n for _ in range(m)]
        dirs = [(0, 1), (1, 0), (0, -1), (-1, 0)]
        i = j = d = 0
        res = []
        for _ in range(m * n):
            res.append(matrix[i][j])
            seen[i][j] = True
            ni, nj = i + dirs[d][0], j + dirs[d][1]
            if not (0 <= ni < m and 0 <= nj < n) or seen[ni][nj]:
                d = (d + 1) % 4
                ni, nj = i + dirs[d][0], j + dirs[d][1]
            i, j = ni, nj
        return res
