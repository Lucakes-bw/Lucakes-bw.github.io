class Solution:
    def combinationSum(self, candidates: List[int], target: int) -> List[List[int]]:
        # 完全背包式的递推：ways[t] 是和为 t 的所有组合，按候选数依次加入保证不重复
        ways = [[] for _ in range(target + 1)]
        ways[0] = [[]]
        for c in candidates:
            for t in range(c, target + 1):
                ways[t] += [w + [c] for w in ways[t - c]]
        return ways[target]
