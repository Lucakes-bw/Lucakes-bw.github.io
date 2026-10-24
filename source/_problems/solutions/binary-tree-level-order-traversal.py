class Solution:
    def levelOrder(self, root: Optional[TreeNode]) -> List[List[int]]:
        res, q = [], deque([root] if root else [])
        while q:
            res.append([])
            for _ in range(len(q)):
                n = q.popleft()
                res[-1].append(n.val)
                if n.left: q.append(n.left)
                if n.right: q.append(n.right)
        return res
