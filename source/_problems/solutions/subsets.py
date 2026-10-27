class Solution:
    def subsets(self, nums: List[int]) -> List[List[int]]:
        res = [[]]
        for x in nums:
            res += [s + [x] for s in res]
        return res
