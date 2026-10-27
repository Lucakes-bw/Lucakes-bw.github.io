class Solution {
public:
    int lengthOfLIS(vector<int>& nums) {
        vector<int> tails;   // tails[k]：长度为 k + 1 的递增子序列的最小结尾
        for (int x : nums) {
            auto it = lower_bound(tails.begin(), tails.end(), x);
            if (it == tails.end()) tails.push_back(x);
            else *it = x;
        }
        return tails.size();
    }
};
