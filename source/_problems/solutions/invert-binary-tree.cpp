class Solution {
public:
    TreeNode* invertTree(TreeNode* root) {
        if (!root) return nullptr;
        TreeNode* l = invertTree(root->left);
        root->left = invertTree(root->right);
        root->right = l;
        return root;
    }
};
