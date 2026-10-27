class Solution {
    bool dfs(vector<vector<char>>& b, const string& w, int i, int j, int k) {
        if (i < 0 || j < 0 || i >= (int)b.size() || j >= (int)b[0].size() || b[i][j] != w[k]) return false;
        if (k + 1 == (int)w.size()) return true;
        char c = b[i][j];
        b[i][j] = '#';   // 标记走过
        bool ok = dfs(b, w, i + 1, j, k + 1) || dfs(b, w, i - 1, j, k + 1) || dfs(b, w, i, j + 1, k + 1) || dfs(b, w, i, j - 1, k + 1);
        b[i][j] = c;
        return ok;
    }
public:
    bool exist(vector<vector<char>>& board, string word) {
        // 剪枝：网格里某个字母不够用，直接失败；从出现次数较少的一端开始搜
        unordered_map<char, int> cnt;
        for (auto& row : board) for (char c : row) cnt[c]++;
        for (char c : word) if (--cnt[c] < 0) return false;
        if (cnt[word.back()] < cnt[word[0]]) reverse(word.begin(), word.end());
        for (int i = 0; i < (int)board.size(); i++)
            for (int j = 0; j < (int)board[0].size(); j++)
                if (dfs(board, word, i, j, 0)) return true;
        return false;
    }
};
