class Solution:
    def canJump(self, nums: List[int]) -> bool:
        # 倒着看：goal 是目前已知能到达终点的最左位置
        goal = len(nums) - 1
        for i in range(len(nums) - 2, -1, -1):
            if i + nums[i] >= goal:
                goal = i
        return goal == 0
