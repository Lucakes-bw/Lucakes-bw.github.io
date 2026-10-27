var partition = function(s) {
    const res = [], path = [];
    const isPal = (i, j) => { while (i < j) if (s[i++] !== s[j--]) return false; return true; };
    const go = i => {
        if (i === s.length) { res.push([...path]); return; }
        for (let j = i; j < s.length; j++)
            if (isPal(i, j)) {
                path.push(s.slice(i, j + 1));
                go(j + 1);
                path.pop();
            }
    };
    go(0);
    return res;
};
