class Solution:
    def rob(self, nums: List[int]) -> int:
        prev, cur = 0, 0   # f[i-2], f[i-1]
        for x in nums:
            prev, cur = cur, max(cur, prev + x)
        return cur
