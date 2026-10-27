class Solution:
    def climbStairs(self, n: int) -> int:
        @cache
        def f(i):
            return 1 if i <= 1 else f(i - 1) + f(i - 2)
        return f(n)
