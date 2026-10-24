var levelOrder = function(root) {
    const res = [];
    let level = root ? [root] : [];
    while (level.length) {
        res.push(level.map(n => n.val));
        level = level.flatMap(n => [n.left, n.right]).filter(Boolean);
    }
    return res;
};
