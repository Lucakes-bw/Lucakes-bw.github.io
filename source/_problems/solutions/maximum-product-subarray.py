class Solution:
    def maxProduct(self, nums: List[int]) -> int:
        mx = mn = best = nums[0]
        for x in nums[1:]:
            mx, mn = max(x, mx * x, mn * x), min(x, mx * x, mn * x)
            best = max(best, mx)
        return best
