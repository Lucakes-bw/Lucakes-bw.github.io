var maxProduct = function(nums) {
    let mx = nums[0], mn = nums[0], best = nums[0];
    for (let i = 1; i < nums.length; i++) {
        const x = nums[i], a = mx * x, b = mn * x;
        mx = Math.max(x, a, b);
        mn = Math.min(x, a, b);
        best = Math.max(best, mx);
    }
    return best + 0;   // 把 -0 变成 0
};
