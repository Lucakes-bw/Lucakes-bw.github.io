var permute = function(nums) {
    const res = [], path = [], used = new Array(nums.length).fill(false);
    const go = () => {
        if (path.length === nums.length) { res.push([...path]); return; }
        nums.forEach((x, i) => {
            if (used[i]) return;
            used[i] = true; path.push(x);
            go();
            path.pop(); used[i] = false;
        });
    };
    go();
    return res;
};
