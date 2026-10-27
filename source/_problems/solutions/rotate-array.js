var rotate = function(nums, k) {
    const n = nums.length;
    k %= n;
    const rev = (l, r) => { for (; l < r; l++, r--) [nums[l], nums[r]] = [nums[r], nums[l]]; };
    rev(0, n - 1);
    rev(0, k - 1);
    rev(k, n - 1);
};
