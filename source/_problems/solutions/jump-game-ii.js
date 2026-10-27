var jump = function(nums) {
    // 按层 BFS：[l, r] 是用 steps 跳能到达的区间
    let l = 0, r = 0, steps = 0;
    while (r < nums.length - 1) {
        let far = r;
        for (let i = l; i <= r; i++) far = Math.max(far, i + nums[i]);
        l = r + 1, r = far, steps++;
    }
    return steps;
};
