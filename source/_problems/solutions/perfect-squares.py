class Solution:
    def numSquares(self, n: int) -> int:
        # BFS：每一层减去一个完全平方数，第一次减到 0 的层数就是答案
        squares = [i * i for i in range(1, math.isqrt(n) + 1)]
        level, seen, depth = {n}, {n}, 0
        while level:
            depth += 1
            nxt = set()
            for x in level:
                for s in squares:
                    if s > x:
                        break
                    if s == x:
                        return depth
                    if x - s not in seen:
                        seen.add(x - s)
                        nxt.add(x - s)
            level = nxt
        return depth
