class Solution {
public:
    string longestPalindrome(string s) {
        int n = s.size(), start = 0, len = 1;
        auto expand = [&](int l, int r) {
            while (l >= 0 && r < n && s[l] == s[r]) l--, r++;
            if (r - l - 1 > len) start = l + 1, len = r - l - 1;
        };
        for (int i = 0; i < n; i++) {
            expand(i, i);       // 奇数长度
            expand(i, i + 1);   // 偶数长度
        }
        return s.substr(start, len);
    }
};
