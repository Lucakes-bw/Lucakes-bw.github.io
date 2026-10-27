class Solution:
    def search(self, nums: List[int], target: int) -> int:
        # 先二分找到最小值的位置（旋转点），再在对应的有序段里二分
        l, r = 0, len(nums) - 1
        while l < r:
            m = (l + r) // 2
            if nums[m] > nums[r]:
                l = m + 1
            else:
                r = m
        k = l
        lo, hi = (k, len(nums)) if target <= nums[-1] else (0, k)
        i = bisect_left(nums, target, lo, hi)
        return i if i < hi and nums[i] == target else -1
