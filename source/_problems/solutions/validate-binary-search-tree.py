class Solution:
    def isValidBST(self, root: Optional[TreeNode]) -> bool:
        # 中序遍历必须严格递增
        prev, st, cur = None, [], root
        while cur or st:
            while cur:
                st.append(cur)
                cur = cur.left
            cur = st.pop()
            if prev is not None and cur.val <= prev:
                return False
            prev, cur = cur.val, cur.right
        return True
