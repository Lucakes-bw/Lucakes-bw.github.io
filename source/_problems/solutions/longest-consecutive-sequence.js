var longestConsecutive = function(nums) {
    const s = new Set(nums);
    let best = 0;
    for (const x of s) {
        if (s.has(x - 1)) continue;
        let y = x;
        while (s.has(y + 1)) y++;
        best = Math.max(best, y - x + 1);
    }
    return best;
};
