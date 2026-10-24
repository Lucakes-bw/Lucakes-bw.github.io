class Solution:
    def search(self, nums: List[int], target: int) -> int:
        i = bisect_left(nums, target)
        return i if i < len(nums) and nums[i] == target else -1
