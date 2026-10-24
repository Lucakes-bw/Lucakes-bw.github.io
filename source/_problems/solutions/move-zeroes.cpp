class Solution {
public:
    void moveZeroes(vector<int>& nums) {
        int k = 0;
        for (int x : nums) if (x != 0) nums[k++] = x;
        while (k < (int)nums.size()) nums[k++] = 0;
    }
};
