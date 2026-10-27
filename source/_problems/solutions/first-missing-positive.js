var firstMissingPositive = function(nums) {
    const n = nums.length;
    // 不在 [1, n] 的数先改成 n + 1；再用负号标记「数值 x 出现过」
    for (let i = 0; i < n; i++) if (nums[i] <= 0 || nums[i] > n) nums[i] = n + 1;
    for (let i = 0; i < n; i++) {
        const x = Math.abs(nums[i]);
        if (x <= n && nums[x - 1] > 0) nums[x - 1] = -nums[x - 1];
    }
    for (let i = 0; i < n; i++) if (nums[i] > 0) return i + 1;
    return n + 1;
};
