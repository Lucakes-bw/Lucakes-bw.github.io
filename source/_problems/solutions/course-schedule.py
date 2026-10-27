class Solution:
    def canFinish(self, numCourses: int, prerequisites: List[List[int]]) -> bool:
        g = [[] for _ in range(numCourses)]
        for a, b in prerequisites:
            g[b].append(a)
        # 0 未访问，1 在当前 DFS 路径上，2 已确认无环
        color = [0] * numCourses
        def has_cycle(u):
            color[u] = 1
            for v in g[u]:
                if color[v] == 1 or (color[v] == 0 and has_cycle(v)):
                    return True
            color[u] = 2
            return False
        return not any(color[u] == 0 and has_cycle(u) for u in range(numCourses))
