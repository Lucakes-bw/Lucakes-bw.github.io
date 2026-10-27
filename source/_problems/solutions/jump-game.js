var canJump = function(nums) {
    let far = 0;
    for (let i = 0; i < nums.length && i <= far; i++) far = Math.max(far, i + nums[i]);
    return far >= nums.length - 1;
};
