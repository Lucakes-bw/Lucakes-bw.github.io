class Solution {
public:
    int minPathSum(vector<vector<int>>& grid) {
        int m = grid.size(), n = grid[0].size();
        for (int i = 0; i < m; i++)
            for (int j = 0; j < n; j++) {
                if (i == 0 && j == 0) continue;
                int up = i ? grid[i - 1][j] : INT_MAX, left = j ? grid[i][j - 1] : INT_MAX;
                grid[i][j] += min(up, left);
            }
        return grid[m - 1][n - 1];
    }
};
