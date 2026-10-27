class Solution {
public:
    int trap(vector<int>& height) {
        int l = 0, r = height.size() - 1, lmax = 0, rmax = 0, res = 0;
        while (l < r) {
            lmax = max(lmax, height[l]);
            rmax = max(rmax, height[r]);
            // 较矮的一侧，积水高度已经由它这边的最大值决定
            if (lmax < rmax) res += lmax - height[l++];
            else res += rmax - height[r--];
        }
        return res;
    }
};
