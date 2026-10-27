class Solution {
public:
    double findMedianSortedArrays(vector<int>& nums1, vector<int>& nums2) {
        if (nums1.size() > nums2.size()) swap(nums1, nums2);
        int m = nums1.size(), n = nums2.size(), half = (m + n + 1) / 2;
        // 在短数组上二分分割线：左边取 nums1 的前 i 个、nums2 的前 half - i 个
        int l = 0, r = m;
        while (l < r) {
            int i = (l + r + 1) / 2, j = half - i;
            if (nums1[i - 1] > nums2[j]) r = i - 1;
            else l = i;
        }
        int i = l, j = half - i;
        int leftMax = max(i ? nums1[i - 1] : INT_MIN, j ? nums2[j - 1] : INT_MIN);
        if ((m + n) % 2) return leftMax;
        int rightMin = min(i < m ? nums1[i] : INT_MAX, j < n ? nums2[j] : INT_MAX);
        return (leftMax + (double)rightMin) / 2;
    }
};
