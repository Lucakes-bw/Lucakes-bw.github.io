var isSymmetric = function(root) {
    const mirror = (a, b) => !a || !b ? a === b
        : a.val === b.val && mirror(a.left, b.right) && mirror(a.right, b.left);
    return mirror(root.left, root.right);
};
