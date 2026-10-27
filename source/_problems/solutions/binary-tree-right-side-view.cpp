class Solution {
    vector<int> res;
    void dfs(TreeNode* n, int depth) {
        if (!n) return;
        if (depth == (int)res.size()) res.push_back(n->val);   // 这一层第一个到达的
        dfs(n->right, depth + 1);
        dfs(n->left, depth + 1);
    }
public:
    vector<int> rightSideView(TreeNode* root) {
        dfs(root, 0);
        return res;
    }
};
