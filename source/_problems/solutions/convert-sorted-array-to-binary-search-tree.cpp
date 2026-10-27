class Solution {
    TreeNode* build(vector<int>& nums, int l, int r) {
        if (l > r) return nullptr;
        int m = (l + r) / 2;
        return new TreeNode(nums[m], build(nums, l, m - 1), build(nums, m + 1, r));
    }
public:
    TreeNode* sortedArrayToBST(vector<int>& nums) {
        return build(nums, 0, nums.size() - 1);
    }
};
