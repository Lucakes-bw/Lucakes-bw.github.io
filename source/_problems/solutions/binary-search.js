var search = function(nums, target) {
    let l = 0, r = nums.length;
    while (l < r) {
        const m = (l + r) >> 1;
        if (nums[m] < target) l = m + 1; else r = m;
    }
    return nums[l] === target ? l : -1;
};
