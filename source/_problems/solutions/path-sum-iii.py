class Solution:
    def pathSum(self, root: Optional[TreeNode], targetSum: int) -> int:
        cnt = Counter({0: 1})
        def dfs(n, pre):
            if not n:
                return 0
            pre += n.val
            res = cnt[pre - targetSum]
            cnt[pre] += 1
            res += dfs(n.left, pre) + dfs(n.right, pre)
            cnt[pre] -= 1
            return res
        return dfs(root, 0)
