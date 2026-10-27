class Solution {
public:
    int lengthOfLongestSubstring(string s) {
        vector<int> last(128, -1);
        int best = 0;
        for (int l = 0, r = 0; r < (int)s.size(); r++) {
            l = max(l, last[(unsigned char)s[r]] + 1);
            last[(unsigned char)s[r]] = r;
            best = max(best, r - l + 1);
        }
        return best;
    }
};
