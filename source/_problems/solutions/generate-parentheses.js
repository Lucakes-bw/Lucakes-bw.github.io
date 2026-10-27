var generateParenthesis = function(n) {
    const res = [];
    const go = (s, open, close) => {
        if (s.length === 2 * n) { res.push(s); return; }
        if (open < n) go(s + '(', open + 1, close);
        if (close < open) go(s + ')', open, close + 1);
    };
    go('', 0, 0);
    return res;
};
