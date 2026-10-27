class Solution {
public:
    vector<vector<string>> solveNQueens(int n) {
        vector<vector<string>> res;
        vector<string> board(n, string(n, '.'));
        vector<bool> col(n), d1(2 * n), d2(2 * n);   // d1：row - col + n，d2：row + col
        function<void(int)> go = [&](int r) {
            if (r == n) { res.push_back(board); return; }
            for (int c = 0; c < n; c++) {
                if (col[c] || d1[r - c + n] || d2[r + c]) continue;
                col[c] = d1[r - c + n] = d2[r + c] = true;
                board[r][c] = 'Q';
                go(r + 1);
                board[r][c] = '.';
                col[c] = d1[r - c + n] = d2[r + c] = false;
            }
        };
        go(0);
        return res;
    }
};
