class Solution {
public:
    vector<vector<string>> partition(string s) {
        int n = s.size();
        vector<vector<bool>> pal(n, vector<bool>(n, false));   // pal[i][j]：s[i..j] 是回文
        for (int i = n - 1; i >= 0; i--)
            for (int j = i; j < n; j++)
                pal[i][j] = s[i] == s[j] && (j - i < 2 || pal[i + 1][j - 1]);
        vector<vector<string>> res;
        vector<string> path;
        function<void(int)> go = [&](int i) {
            if (i == n) { res.push_back(path); return; }
            for (int j = i; j < n; j++)
                if (pal[i][j]) {
                    path.push_back(s.substr(i, j - i + 1));
                    go(j + 1);
                    path.pop_back();
                }
        };
        go(0);
        return res;
    }
};
