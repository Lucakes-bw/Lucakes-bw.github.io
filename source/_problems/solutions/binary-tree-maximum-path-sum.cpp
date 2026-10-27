class Solution {
    int best = INT_MIN;
    int gain(TreeNode* n) {
        if (!n) return 0;
        int l = max(gain(n->left), 0), r = max(gain(n->right), 0);
        best = max(best, n->val + l + r);
        return n->val + max(l, r);
    }
public:
    int maxPathSum(TreeNode* root) {
        gain(root);
        return best;
    }
};
