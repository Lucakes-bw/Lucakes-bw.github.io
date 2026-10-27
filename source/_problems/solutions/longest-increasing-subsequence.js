var lengthOfLIS = function(nums) {
    // O(n²) 动态规划：f[i] 是以 nums[i] 结尾的最长递增子序列长度
    const f = new Array(nums.length).fill(1);
    for (let i = 1; i < nums.length; i++)
        for (let j = 0; j < i; j++)
            if (nums[j] < nums[i]) f[i] = Math.max(f[i], f[j] + 1);
    return Math.max(...f);
};
