class Solution:
    def buildTree(self, preorder: List[int], inorder: List[int]) -> Optional[TreeNode]:
        pos = {v: i for i, v in enumerate(inorder)}
        def build(pl, il, n):   # 先序从 pl 开始、中序从 il 开始的 n 个节点
            if n == 0:
                return None
            v = preorder[pl]
            m = pos[v] - il   # 左子树的大小
            return TreeNode(v, build(pl + 1, il, m), build(pl + 1 + m, il + m + 1, n - 1 - m))
        return build(0, 0, len(preorder))
