var trap = function(height) {
    const n = height.length, right = new Array(n);
    for (let i = n - 1, m = 0; i >= 0; i--) right[i] = m = Math.max(m, height[i]);
    let left = 0, res = 0;
    for (let i = 0; i < n; i++) {
        left = Math.max(left, height[i]);
        res += Math.min(left, right[i]) - height[i];
    }
    return res;
};
