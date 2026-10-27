class Solution:
    def permute(self, nums: List[int]) -> List[List[int]]:
        res, path, used = [], [], [False] * len(nums)
        def go():
            if len(path) == len(nums):
                res.append(path[:])
                return
            for i, x in enumerate(nums):
                if not used[i]:
                    used[i] = True
                    path.append(x)
                    go()
                    path.pop()
                    used[i] = False
        go()
        return res
