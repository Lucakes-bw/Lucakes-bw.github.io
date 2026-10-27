var majorityElement = function(nums) {
    let cand = 0, cnt = 0;
    for (const x of nums) {
        if (cnt === 0) cand = x;
        cnt += x === cand ? 1 : -1;
    }
    return cand;
};
