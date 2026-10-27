var threeSum = function(nums) {
    nums.sort((a, b) => a - b);
    const n = nums.length, res = [];
    for (let i = 0; i < n - 2; i++) {
        if (i && nums[i] === nums[i - 1]) continue;
        let l = i + 1, r = n - 1;
        while (l < r) {
            const s = nums[i] + nums[l] + nums[r];
            if (s < 0) l++;
            else if (s > 0) r--;
            else {
                res.push([nums[i], nums[l], nums[r]]);
                while (l < r && nums[l] === nums[l + 1]) l++;
                while (l < r && nums[r] === nums[r - 1]) r--;
                l++, r--;
            }
        }
    }
    return res;
};
