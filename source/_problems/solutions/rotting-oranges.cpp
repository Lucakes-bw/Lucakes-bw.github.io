class Solution {
public:
    int orangesRotting(vector<vector<int>>& grid) {
        int m = grid.size(), n = grid[0].size(), fresh = 0, minutes = 0;
        queue<pair<int, int>> q;
        for (int i = 0; i < m; i++)
            for (int j = 0; j < n; j++) {
                if (grid[i][j] == 2) q.push({i, j});
                else if (grid[i][j] == 1) fresh++;
            }
        int dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        while (fresh && !q.empty()) {
            minutes++;
            for (int s = q.size(); s > 0; s--) {
                auto [i, j] = q.front(); q.pop();
                for (auto& d : dirs) {
                    int x = i + d[0], y = j + d[1];
                    if (x < 0 || x >= m || y < 0 || y >= n || grid[x][y] != 1) continue;
                    grid[x][y] = 2;
                    fresh--;
                    q.push({x, y});
                }
            }
        }
        return fresh ? -1 : minutes;
    }
};
