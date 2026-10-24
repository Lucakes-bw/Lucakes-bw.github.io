var twoSum = function(nums, target) {
    const pos = new Map();
    for (let i = 0; i < nums.length; i++) {
        if (pos.has(target - nums[i])) return [pos.get(target - nums[i]), i];
        pos.set(nums[i], i);
    }
    return [];
};
