class Solution:
    def diameterOfBinaryTree(self, root: Optional[TreeNode]) -> int:
        best = 0
        def depth(n):
            nonlocal best
            if not n:
                return 0
            l, r = depth(n.left), depth(n.right)
            best = max(best, l + r)
            return max(l, r) + 1
        depth(root)
        return best
