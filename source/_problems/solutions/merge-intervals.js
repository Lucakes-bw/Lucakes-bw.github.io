var merge = function(intervals) {
    intervals.sort((a, b) => a[0] - b[0]);
    const res = [];
    for (const [a, b] of intervals) {
        const last = res[res.length - 1];
        if (last && a <= last[1]) last[1] = Math.max(last[1], b);
        else res.push([a, b]);
    }
    return res;
};
