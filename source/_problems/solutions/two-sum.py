class Solution:
    def twoSum(self, nums: List[int], target: int) -> List[int]:
        pos = {}
        for i, x in enumerate(nums):
            if target - x in pos:
                return [pos[target - x], i]
            pos[x] = i
        return []
