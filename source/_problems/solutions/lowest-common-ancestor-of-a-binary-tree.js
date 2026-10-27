var lowestCommonAncestor = function(root, p, q) {
    if (!root || root === p || root === q) return root;
    const l = lowestCommonAncestor(root.left, p, q), r = lowestCommonAncestor(root.right, p, q);
    return l && r ? root : l || r;
};
