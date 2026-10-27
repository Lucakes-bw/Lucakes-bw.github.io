class Solution {
public:
    bool canPartition(vector<int>& nums) {
        int sum = accumulate(nums.begin(), nums.end(), 0);
        if (sum % 2) return false;
        bitset<10001> f;   // f[j]：能否凑出和 j
        f[0] = 1;
        for (int x : nums) f |= f << x;
        return f[sum / 2];
    }
};
