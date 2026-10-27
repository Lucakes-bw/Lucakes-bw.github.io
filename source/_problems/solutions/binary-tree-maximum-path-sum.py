class Solution:
    def maxPathSum(self, root: Optional[TreeNode]) -> int:
        best = -inf
        def gain(n):
            nonlocal best
            if not n:
                return 0
            l, r = max(gain(n.left), 0), max(gain(n.right), 0)
            best = max(best, n.val + l + r)
            return n.val + max(l, r)
        gain(root)
        return best
