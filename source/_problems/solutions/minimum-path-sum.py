class Solution:
    def minPathSum(self, grid: List[List[int]]) -> int:
        f = list(accumulate(grid[0]))
        for row in grid[1:]:
            f[0] += row[0]
            for j in range(1, len(row)):
                f[j] = row[j] + min(f[j], f[j - 1])
        return f[-1]
