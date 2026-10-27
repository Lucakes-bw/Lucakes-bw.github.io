class Solution {
public:
    int findDuplicate(vector<int>& nums) {
        // 把 i → nums[i] 看成链表，重复的数就是环的入口
        int slow = 0, fast = 0;
        do slow = nums[slow], fast = nums[nums[fast]]; while (slow != fast);
        slow = 0;
        while (slow != fast) slow = nums[slow], fast = nums[fast];
        return slow;
    }
};
