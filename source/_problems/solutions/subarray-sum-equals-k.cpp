class Solution {
public:
    int subarraySum(vector<int>& nums, int k) {
        unordered_map<int, int> cnt{{0, 1}};
        int pre = 0, res = 0;
        for (int x : nums) {
            pre += x;
            auto it = cnt.find(pre - k);
            if (it != cnt.end()) res += it->second;
            cnt[pre]++;
        }
        return res;
    }
};
