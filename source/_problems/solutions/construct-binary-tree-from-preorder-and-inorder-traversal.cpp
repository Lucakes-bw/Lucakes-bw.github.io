class Solution {
    unordered_map<int, int> pos;
    int k = 0;   // 先序序列里下一个要用的根
    TreeNode* build(vector<int>& pre, int l, int r) {   // 中序区间 [l, r]
        if (l > r) return nullptr;
        TreeNode* root = new TreeNode(pre[k++]);
        int m = pos[root->val];
        root->left = build(pre, l, m - 1);
        root->right = build(pre, m + 1, r);
        return root;
    }
public:
    TreeNode* buildTree(vector<int>& preorder, vector<int>& inorder) {
        for (int i = 0; i < (int)inorder.size(); i++) pos[inorder[i]] = i;
        return build(preorder, 0, inorder.size() - 1);
    }
};
