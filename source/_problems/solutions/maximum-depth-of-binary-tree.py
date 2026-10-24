class Solution:
    def maxDepth(self, root: Optional[TreeNode]) -> int:
        depth, level = 0, [root] if root else []
        while level:
            depth += 1
            level = [c for n in level for c in (n.left, n.right) if c]
        return depth
