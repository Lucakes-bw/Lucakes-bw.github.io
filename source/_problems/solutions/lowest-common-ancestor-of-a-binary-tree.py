class Solution:
    def lowestCommonAncestor(self, root: 'TreeNode', p: 'TreeNode', q: 'TreeNode') -> 'TreeNode':
        # 记录每个节点的父节点，从 p 往上收集祖先，再从 q 往上找第一个公共的
        parent, st = {root: None}, [root]
        while p not in parent or q not in parent:
            n = st.pop()
            for c in (n.left, n.right):
                if c:
                    parent[c] = n
                    st.append(c)
        anc = set()
        while p:
            anc.add(p)
            p = parent[p]
        while q not in anc:
            q = parent[q]
        return q
