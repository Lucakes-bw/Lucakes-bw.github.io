class Solution {
public:
    int maxSubArray(vector<int>& nums) {
        int cur = 0, best = INT_MIN;
        for (int x : nums) {
            cur = max(cur, 0) + x;
            best = max(best, cur);
        }
        return best;
    }
};
