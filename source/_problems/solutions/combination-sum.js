var combinationSum = function(candidates, target) {
    candidates.sort((a, b) => a - b);
    const res = [], path = [];
    const go = (i, rest) => {
        if (rest === 0) { res.push([...path]); return; }
        for (let j = i; j < candidates.length && candidates[j] <= rest; j++) {
            path.push(candidates[j]);
            go(j, rest - candidates[j]);
            path.pop();
        }
    };
    go(0, target);
    return res;
};
