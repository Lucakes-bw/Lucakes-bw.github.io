class Solution:
    def generate(self, numRows: int) -> List[List[int]]:
        res = [[1]]
        for _ in range(numRows - 1):
            prev = res[-1]
            res.append([a + b for a, b in zip([0] + prev, prev + [0])])
        return res
