var productExceptSelf = function(nums) {
    const n = nums.length, res = new Array(n).fill(1);
    for (let i = 1; i < n; i++) res[i] = res[i - 1] * nums[i - 1];
    for (let i = n - 1, suf = 1; i >= 0; i--) {
        res[i] *= suf;
        suf *= nums[i];
    }
    return res.map(x => x + 0);   // 把 -0 变成 0
};
