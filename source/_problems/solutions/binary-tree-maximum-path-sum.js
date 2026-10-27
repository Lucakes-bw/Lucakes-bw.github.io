var maxPathSum = function(root) {
    let best = -Infinity;
    const gain = n => {
        if (!n) return 0;
        const l = Math.max(gain(n.left), 0), r = Math.max(gain(n.right), 0);
        best = Math.max(best, n.val + l + r);
        return n.val + Math.max(l, r);
    };
    gain(root);
    return best;
};
