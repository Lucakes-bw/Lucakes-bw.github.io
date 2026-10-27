class Solution {
public:
    void flatten(TreeNode* root) {
        for (TreeNode* cur = root; cur; cur = cur->right) {
            if (!cur->left) continue;
            TreeNode* p = cur->left;
            while (p->right) p = p->right;   // 左子树最右边的节点
            p->right = cur->right;
            cur->right = cur->left;
            cur->left = nullptr;
        }
    }
};
