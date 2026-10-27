var searchRange = function(nums, target) {
    const lower = x => {
        let l = 0, r = nums.length;
        while (l < r) {
            const m = (l + r) >> 1;
            if (nums[m] < x) l = m + 1;
            else r = m;
        }
        return l;
    };
    const a = lower(target);
    if (a === nums.length || nums[a] !== target) return [-1, -1];
    return [a, lower(target + 1) - 1];
};
