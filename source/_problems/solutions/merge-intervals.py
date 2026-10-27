class Solution:
    def merge(self, intervals: List[List[int]]) -> List[List[int]]:
        res = []
        for a, b in sorted(intervals):
            if res and a <= res[-1][1]:
                res[-1][1] = max(res[-1][1], b)
            else:
                res.append([a, b])
        return res
