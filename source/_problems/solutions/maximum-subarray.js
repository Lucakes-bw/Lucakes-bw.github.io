var maxSubArray = function(nums) {
    let cur = 0, best = -Infinity;
    for (const x of nums) {
        cur = Math.max(cur, 0) + x;
        best = Math.max(best, cur);
    }
    return best;
};
