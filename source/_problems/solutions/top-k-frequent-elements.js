var topKFrequent = function(nums, k) {
    const cnt = new Map();
    for (const x of nums) cnt.set(x, (cnt.get(x) || 0) + 1);
    return [...cnt.entries()].sort((a, b) => b[1] - a[1]).slice(0, k).map(e => e[0]);
};
