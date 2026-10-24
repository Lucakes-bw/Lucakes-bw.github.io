class Solution:
    def numIslands(self, grid: List[List[str]]) -> int:
        m, n, cnt = len(grid), len(grid[0]), 0
        for i in range(m):
            for j in range(n):
                if grid[i][j] != '1':
                    continue
                cnt += 1
                grid[i][j] = '0'
                q = deque([(i, j)])
                while q:
                    x, y = q.popleft()
                    for a, b in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                        if 0 <= a < m and 0 <= b < n and grid[a][b] == '1':
                            grid[a][b] = '0'
                            q.append((a, b))
        return cnt
