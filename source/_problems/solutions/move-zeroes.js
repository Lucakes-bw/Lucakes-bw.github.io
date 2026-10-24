var moveZeroes = function(nums) {
    let k = 0;
    for (const x of nums) if (x !== 0) nums[k++] = x;
    nums.fill(0, k);
};
