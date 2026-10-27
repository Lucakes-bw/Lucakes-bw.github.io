class Solution:
    def findDuplicate(self, nums: List[int]) -> int:
        # 对答案二分：<= mid 的数超过 mid 个，说明重复的数在 [1, mid] 里
        l, r = 1, len(nums) - 1
        while l < r:
            mid = (l + r) // 2
            if sum(x <= mid for x in nums) > mid:
                r = mid
            else:
                l = mid + 1
        return l
