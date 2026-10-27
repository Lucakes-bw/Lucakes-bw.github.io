var partitionLabels = function(s) {
    const last = {};
    [...s].forEach((c, i) => last[c] = i);
    const res = [];
    let start = 0, end = 0;
    for (let i = 0; i < s.length; i++) {
        end = Math.max(end, last[s[i]]);
        if (i === end) res.push(end - start + 1), start = i + 1;
    }
    return res;
};
