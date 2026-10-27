class Solution:
    def flatten(self, root: Optional[TreeNode]) -> None:
        # 按「右、左、根」的顺序访问（先序的逆序），把每个节点接到上一个访问的节点前面
        prev = None
        def go(n):
            nonlocal prev
            if not n:
                return
            go(n.right)
            go(n.left)
            n.right, n.left = prev, None
            prev = n
        go(root)
