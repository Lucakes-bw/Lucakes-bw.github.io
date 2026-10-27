var kthSmallest = function(root, k) {
    let res;
    const go = n => {
        if (!n || k === 0) return;
        go(n.left);
        if (--k === 0) res = n.val;
        go(n.right);
    };
    go(root);
    return res;
};
