var sortColors = function(nums) {
    let p0 = 0, i = 0, p2 = nums.length - 1;
    while (i <= p2) {
        if (nums[i] === 0) [nums[i], nums[p0]] = [nums[p0], nums[i]], i++, p0++;
        else if (nums[i] === 2) [nums[i], nums[p2]] = [nums[p2], nums[i]], p2--;
        else i++;
    }
};
