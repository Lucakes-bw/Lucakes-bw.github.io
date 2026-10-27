class Solution:
    def majorityElement(self, nums: List[int]) -> int:
        # 排序后正中间的一定是多数元素
        return sorted(nums)[len(nums) // 2]
