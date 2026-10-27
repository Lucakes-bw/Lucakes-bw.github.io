class Solution:
    def invertTree(self, root: Optional[TreeNode]) -> Optional[TreeNode]:
        q = deque([root] if root else [])
        while q:
            n = q.popleft()
            n.left, n.right = n.right, n.left
            q.extend(c for c in (n.left, n.right) if c)
        return root
