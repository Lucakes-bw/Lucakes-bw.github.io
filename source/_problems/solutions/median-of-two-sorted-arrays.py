class Solution:
    def findMedianSortedArrays(self, nums1: List[int], nums2: List[int]) -> float:
        def kth(a, i, b, j, k):   # a[i:] 和 b[j:] 合起来第 k 小（k 从 1 开始）
            if i == len(a):
                return b[j + k - 1]
            if j == len(b):
                return a[i + k - 1]
            if k == 1:
                return min(a[i], b[j])
            p = min(len(a) - i, k // 2)
            q = min(len(b) - j, k // 2)
            if a[i + p - 1] <= b[j + q - 1]:
                return kth(a, i + p, b, j, k - p)
            return kth(a, i, b, j + q, k - q)
        n = len(nums1) + len(nums2)
        if n % 2:
            return float(kth(nums1, 0, nums2, 0, n // 2 + 1))
        return (kth(nums1, 0, nums2, 0, n // 2) + kth(nums1, 0, nums2, 0, n // 2 + 1)) / 2
