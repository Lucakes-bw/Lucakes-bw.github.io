class Solution {
    // 取值范围用 long long，节点值可以是 INT_MIN / INT_MAX
    bool check(TreeNode* n, long long lo, long long hi) {
        if (!n) return true;
        if (n->val <= lo || n->val >= hi) return false;
        return check(n->left, lo, n->val) && check(n->right, n->val, hi);
    }
public:
    bool isValidBST(TreeNode* root) {
        return check(root, LLONG_MIN, LLONG_MAX);
    }
};
