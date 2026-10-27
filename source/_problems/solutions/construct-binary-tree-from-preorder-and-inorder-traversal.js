var buildTree = function(preorder, inorder) {
    const pos = new Map(inorder.map((v, i) => [v, i]));
    let k = 0;
    const build = (l, r) => {
        if (l > r) return null;
        const root = new TreeNode(preorder[k++]);
        const m = pos.get(root.val);
        root.left = build(l, m - 1);
        root.right = build(m + 1, r);
        return root;
    };
    return build(0, inorder.length - 1);
};
