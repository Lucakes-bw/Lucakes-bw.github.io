class Solution {
public:
    int rob(vector<int>& nums) {
        int skip = 0, take = 0;   // 上一间不偷 / 偷 时的最大金额
        for (int x : nums) {
            int t = skip + x;
            skip = max(skip, take);
            take = t;
        }
        return max(skip, take);
    }
};
