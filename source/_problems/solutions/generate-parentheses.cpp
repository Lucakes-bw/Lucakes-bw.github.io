class Solution {
    vector<string> res;
    void go(string& s, int open, int close, int n) {
        if ((int)s.size() == 2 * n) { res.push_back(s); return; }
        if (open < n) { s.push_back('('); go(s, open + 1, close, n); s.pop_back(); }
        if (close < open) { s.push_back(')'); go(s, open, close + 1, n); s.pop_back(); }
    }
public:
    vector<string> generateParenthesis(int n) {
        string s;
        go(s, 0, 0, n);
        return res;
    }
};
