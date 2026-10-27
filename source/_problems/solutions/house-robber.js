var rob = function(nums) {
    let prev = 0, cur = 0;
    for (const x of nums) [prev, cur] = [cur, Math.max(cur, prev + x)];
    return cur;
};
