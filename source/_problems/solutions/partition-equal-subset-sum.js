var canPartition = function(nums) {
    const s = nums.reduce((a, b) => a + b, 0);
    if (s % 2) return false;
    const half = s / 2, f = new Array(half + 1).fill(false);
    f[0] = true;
    for (const x of nums)
        for (let j = half; j >= x; j--) f[j] = f[j] || f[j - x];
    return f[half];
};
