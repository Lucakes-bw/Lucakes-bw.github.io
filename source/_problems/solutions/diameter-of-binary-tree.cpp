class Solution {
    int best = 0;
    int depth(TreeNode* n) {
        if (!n) return 0;
        int l = depth(n->left), r = depth(n->right);
        best = max(best, l + r);
        return max(l, r) + 1;
    }
public:
    int diameterOfBinaryTree(TreeNode* root) {
        depth(root);
        return best;
    }
};
