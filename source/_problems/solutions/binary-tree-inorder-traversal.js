var inorderTraversal = function(root) {
    const res = [], st = [];
    let cur = root;
    while (cur || st.length) {
        while (cur) st.push(cur), cur = cur.left;
        cur = st.pop();
        res.push(cur.val);
        cur = cur.right;
    }
    return res;
};
