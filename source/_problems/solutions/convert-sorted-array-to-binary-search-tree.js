var sortedArrayToBST = function(nums) {
    const build = (l, r) => {
        if (l > r) return null;
        const m = (l + r) >> 1;
        return new TreeNode(nums[m], build(l, m - 1), build(m + 1, r));
    };
    return build(0, nums.length - 1);
};
