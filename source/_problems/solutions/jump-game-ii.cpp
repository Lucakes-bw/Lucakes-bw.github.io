class Solution {
public:
    int jump(vector<int>& nums) {
        int steps = 0, end = 0, far = 0;   // end：当前这一跳能到的最远位置
        for (int i = 0; i + 1 < (int)nums.size(); i++) {
            far = max(far, i + nums[i]);
            if (i == end) steps++, end = far;
        }
        return steps;
    }
};
