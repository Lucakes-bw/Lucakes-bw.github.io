var isValidBST = function(root) {
    const check = (n, lo, hi) => !n || (n.val > lo && n.val < hi && check(n.left, lo, n.val) && check(n.right, n.val, hi));
    return check(root, -Infinity, Infinity);
};
