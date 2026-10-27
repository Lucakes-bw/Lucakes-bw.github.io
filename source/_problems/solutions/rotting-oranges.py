class Solution:
    def orangesRotting(self, grid: List[List[int]]) -> int:
        m, n = len(grid), len(grid[0])
        rotten = [(i, j) for i in range(m) for j in range(n) if grid[i][j] == 2]
        fresh = sum(row.count(1) for row in grid)
        minutes = 0
        while fresh and rotten:
            minutes += 1
            nxt = []
            for i, j in rotten:
                for x, y in ((i + 1, j), (i - 1, j), (i, j + 1), (i, j - 1)):
                    if 0 <= x < m and 0 <= y < n and grid[x][y] == 1:
                        grid[x][y] = 2
                        fresh -= 1
                        nxt.append((x, y))
            rotten = nxt
        return -1 if fresh else minutes
