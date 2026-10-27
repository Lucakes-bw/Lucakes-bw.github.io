class Solution {
public:
    vector<int> maxSlidingWindow(vector<int>& nums, int k) {
        deque<int> q;   // 下标，对应的值单调递减
        vector<int> res;
        for (int i = 0; i < (int)nums.size(); i++) {
            while (!q.empty() && nums[q.back()] <= nums[i]) q.pop_back();
            q.push_back(i);
            if (q.front() <= i - k) q.pop_front();
            if (i >= k - 1) res.push_back(nums[q.front()]);
        }
        return res;
    }
};
