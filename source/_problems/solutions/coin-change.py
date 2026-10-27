class Solution:
    def coinChange(self, coins: List[int], amount: int) -> int:
        f = [0] + [inf] * amount
        for c in coins:
            for x in range(c, amount + 1):
                f[x] = min(f[x], f[x - c] + 1)
        return f[amount] if f[amount] != inf else -1
