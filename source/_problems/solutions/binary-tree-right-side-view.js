var rightSideView = function(root) {
    const res = [];
    let q = root ? [root] : [];
    while (q.length) {
        res.push(q[q.length - 1].val);
        const next = [];
        for (const n of q) {
            if (n.left) next.push(n.left);
            if (n.right) next.push(n.right);
        }
        q = next;
    }
    return res;
};
