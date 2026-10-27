class Solution {
public:
    void sortColors(vector<int>& nums) {
        int p0 = 0, i = 0, p2 = nums.size() - 1;
        while (i <= p2) {
            if (nums[i] == 0) swap(nums[i++], nums[p0++]);
            else if (nums[i] == 2) swap(nums[i], nums[p2--]);   // 换过来的数还没检查，i 不动
            else i++;
        }
    }
};
