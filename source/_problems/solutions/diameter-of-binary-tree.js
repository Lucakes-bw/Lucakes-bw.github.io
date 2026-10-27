var diameterOfBinaryTree = function(root) {
    let best = 0;
    const depth = n => {
        if (!n) return 0;
        const l = depth(n.left), r = depth(n.right);
        best = Math.max(best, l + r);
        return Math.max(l, r) + 1;
    };
    depth(root);
    return best;
};
