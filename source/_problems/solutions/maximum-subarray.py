class Solution:
    def maxSubArray(self, nums: List[int]) -> int:
        # 分治：最大和要么在左半、要么在右半、要么跨过中点
        def go(l, r):
            if l == r:
                return nums[l]
            m = (l + r) // 2
            s, lbest = 0, -inf
            for i in range(m, l - 1, -1):
                s += nums[i]
                lbest = max(lbest, s)
            s, rbest = 0, -inf
            for i in range(m + 1, r + 1):
                s += nums[i]
                rbest = max(rbest, s)
            return max(go(l, m), go(m + 1, r), lbest + rbest)
        return go(0, len(nums) - 1)
