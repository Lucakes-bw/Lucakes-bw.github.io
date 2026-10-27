var subarraySum = function(nums, k) {
    const cnt = new Map([[0, 1]]);
    let pre = 0, res = 0;
    for (const x of nums) {
        pre += x;
        res += cnt.get(pre - k) || 0;
        cnt.set(pre, (cnt.get(pre) || 0) + 1);
    }
    return res;
};
