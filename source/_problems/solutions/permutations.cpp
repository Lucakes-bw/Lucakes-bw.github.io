class Solution {
    vector<vector<int>> res;
    void go(vector<int>& nums, int k) {   // 前 k 个位置已经确定
        if (k == (int)nums.size()) { res.push_back(nums); return; }
        for (int i = k; i < (int)nums.size(); i++) {
            swap(nums[k], nums[i]);
            go(nums, k + 1);
            swap(nums[k], nums[i]);
        }
    }
public:
    vector<vector<int>> permute(vector<int>& nums) {
        go(nums, 0);
        return res;
    }
};
