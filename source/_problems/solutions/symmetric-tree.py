class Solution:
    def isSymmetric(self, root: Optional[TreeNode]) -> bool:
        q = deque([(root.left, root.right)])
        while q:
            a, b = q.popleft()
            if not a and not b:
                continue
            if not a or not b or a.val != b.val:
                return False
            q.append((a.left, b.right))
            q.append((a.right, b.left))
        return True
