class Solution:
    def jump(self, nums: List[int]) -> int:
        steps = end = far = 0
        for i in range(len(nums) - 1):
            far = max(far, i + nums[i])
            if i == end:
                steps += 1
                end = far
        return steps
