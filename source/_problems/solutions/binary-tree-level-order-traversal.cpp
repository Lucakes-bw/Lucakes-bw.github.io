class Solution {
public:
    vector<vector<int>> levelOrder(TreeNode* root) {
        vector<vector<int>> res;
        queue<TreeNode*> q;
        if (root) q.push(root);
        while (!q.empty()) {
            res.emplace_back();
            for (int n = q.size(); n > 0; n--) {
                TreeNode* t = q.front(); q.pop();
                res.back().push_back(t->val);
                if (t->left) q.push(t->left);
                if (t->right) q.push(t->right);
            }
        }
        return res;
    }
};
