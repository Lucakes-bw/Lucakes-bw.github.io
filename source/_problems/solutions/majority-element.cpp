class Solution {
public:
    int majorityElement(vector<int>& nums) {
        int cand = 0, cnt = 0;
        for (int x : nums) {
            if (cnt == 0) cand = x;
            cnt += x == cand ? 1 : -1;
        }
        return cand;
    }
};
