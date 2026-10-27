class Solution {
    unordered_map<long long, int> cnt{{0, 1}};   // 根到当前节点路径上，各个前缀和出现的次数
    int dfs(TreeNode* n, long long pre, int target) {
        if (!n) return 0;
        pre += n->val;
        int res = cnt.count(pre - target) ? cnt[pre - target] : 0;
        cnt[pre]++;
        res += dfs(n->left, pre, target) + dfs(n->right, pre, target);
        cnt[pre]--;
        return res;
    }
public:
    int pathSum(TreeNode* root, int targetSum) {
        return dfs(root, 0, targetSum);
    }
};
