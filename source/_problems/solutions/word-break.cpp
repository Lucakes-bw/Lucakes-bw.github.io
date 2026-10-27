class Solution {
public:
    bool wordBreak(string s, vector<string>& wordDict) {
        unordered_set<string> dict(wordDict.begin(), wordDict.end());
        size_t maxLen = 0;
        for (auto& w : wordDict) maxLen = max(maxLen, w.size());
        int n = s.size();
        vector<bool> f(n + 1, false);
        f[0] = true;
        for (int i = 1; i <= n; i++)
            for (int j = i - 1; j >= 0 && i - j <= (int)maxLen && !f[i]; j--)
                if (f[j] && dict.count(s.substr(j, i - j))) f[i] = true;
        return f[n];
    }
};
