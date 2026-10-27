var findMedianSortedArrays = function(nums1, nums2) {
    if (nums1.length > nums2.length) [nums1, nums2] = [nums2, nums1];
    const m = nums1.length, n = nums2.length, half = (m + n + 1) >> 1;
    let l = 0, r = m;
    while (l < r) {
        const i = (l + r + 1) >> 1, j = half - i;
        if (nums1[i - 1] > nums2[j]) r = i - 1;
        else l = i;
    }
    const i = l, j = half - i;
    const leftMax = Math.max(i ? nums1[i - 1] : -Infinity, j ? nums2[j - 1] : -Infinity);
    if ((m + n) % 2) return leftMax;
    const rightMin = Math.min(i < m ? nums1[i] : Infinity, j < n ? nums2[j] : Infinity);
    return (leftMax + rightMin) / 2;
};
