var pathSum = function(root, targetSum) {
    // 路径和最多 1000 × 10^9，在 2^53 以内，Number 可以精确表示
    const cnt = new Map([[0, 1]]);
    const dfs = (n, pre) => {
        if (!n) return 0;
        pre += n.val;
        let res = cnt.get(pre - targetSum) || 0;
        cnt.set(pre, (cnt.get(pre) || 0) + 1);
        res += dfs(n.left, pre) + dfs(n.right, pre);
        cnt.set(pre, cnt.get(pre) - 1);
        return res;
    };
    return dfs(root, 0);
};
