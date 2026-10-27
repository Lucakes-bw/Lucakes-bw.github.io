class Solution {
public:
    int maxProduct(vector<int>& nums) {
        int mx = nums[0], mn = nums[0], best = nums[0];
        for (int i = 1; i < (int)nums.size(); i++) {
            int x = nums[i];
            if (x < 0) swap(mx, mn);   // 乘上负数，最大和最小互换
            mx = max(x, mx * x);
            mn = min(x, mn * x);
            best = max(best, mx);
        }
        return best;
    }
};
