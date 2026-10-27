class Solution:
    def inorderTraversal(self, root: Optional[TreeNode]) -> List[int]:
        res = []
        def go(n):
            if n:
                go(n.left)
                res.append(n.val)
                go(n.right)
        go(root)
        return res
