var findMin = function(nums) {
    // 和第一个元素比：比它小的是旋转后的后半段，找第一个这样的位置
    const first = nums[0];
    let l = 0, r = nums.length;
    while (l < r) {
        const m = (l + r) >> 1;
        if (nums[m] < first) r = m;
        else l = m + 1;
    }
    return l === nums.length ? first : nums[l];
};
