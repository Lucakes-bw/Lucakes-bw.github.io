class Solution {
public:
    vector<vector<int>> subsets(vector<int>& nums) {
        int n = nums.size();
        vector<vector<int>> res;
        for (int mask = 0; mask < (1 << n); mask++) {
            res.emplace_back();
            for (int i = 0; i < n; i++)
                if (mask >> i & 1) res.back().push_back(nums[i]);
        }
        return res;
    }
};
