var subsets = function(nums) {
    const res = [], path = [];
    const go = i => {
        if (i === nums.length) { res.push([...path]); return; }
        go(i + 1);                  // 不选 nums[i]
        path.push(nums[i]);
        go(i + 1);                  // 选
        path.pop();
    };
    go(0);
    return res;
};
