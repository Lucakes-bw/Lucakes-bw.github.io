var flatten = function(root) {
    const order = [];
    const pre = n => { if (n) { order.push(n); pre(n.left); pre(n.right); } };
    pre(root);
    for (let i = 0; i < order.length; i++) {
        order[i].left = null;
        order[i].right = order[i + 1] || null;
    }
};
