class Solution:
    def canPartition(self, nums: List[int]) -> bool:
        s = sum(nums)
        if s % 2:
            return False
        half = s // 2
        f = [True] + [False] * half
        for x in nums:
            for j in range(half, x - 1, -1):
                f[j] = f[j] or f[j - x]
        return f[half]
