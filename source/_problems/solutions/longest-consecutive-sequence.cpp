class Solution {
public:
    int longestConsecutive(vector<int>& nums) {
        unordered_set<long long> s(nums.begin(), nums.end());   // long long：x + 1 不会溢出
        int best = 0;
        for (long long x : s) {
            if (s.count(x - 1)) continue;   // 不是一段的起点
            long long y = x;
            while (s.count(y + 1)) y++;
            best = max(best, (int)(y - x + 1));
        }
        return best;
    }
};
