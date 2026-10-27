class Solution {
public:
    vector<int> findAnagrams(string s, string p) {
        int n = s.size(), m = p.size();
        vector<int> res;
        if (m > n) return res;
        array<int, 26> need{}, win{};
        for (char c : p) need[c - 'a']++;
        for (int i = 0; i < n; i++) {
            win[s[i] - 'a']++;
            if (i >= m) win[s[i - m] - 'a']--;
            if (i >= m - 1 && win == need) res.push_back(i - m + 1);
        }
        return res;
    }
};
