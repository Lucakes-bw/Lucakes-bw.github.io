class Solution:
    def longestConsecutive(self, nums: List[int]) -> int:
        s = set(nums)
        best = 0
        for x in s:
            if x - 1 in s:
                continue
            y = x
            while y + 1 in s:
                y += 1
            best = max(best, y - x + 1)
        return best
