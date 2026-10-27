class Solution:
    def searchRange(self, nums: List[int], target: int) -> List[int]:
        def lower(x):   # 第一个 >= x 的位置
            l, r = 0, len(nums)
            while l < r:
                m = (l + r) // 2
                if nums[m] < x:
                    l = m + 1
                else:
                    r = m
            return l
        a = lower(target)
        if a == len(nums) or nums[a] != target:
            return [-1, -1]
        return [a, lower(target + 1) - 1]
