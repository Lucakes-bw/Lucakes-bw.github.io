var longestValidParentheses = function(s) {
    // 左右各扫一遍，只用两个计数器
    let best = 0;
    const scan = (str, open) => {
        let a = 0, b = 0;
        for (const c of str) {
            if (c === open) a++; else b++;
            if (a === b) best = Math.max(best, 2 * a);
            else if (b > a) a = b = 0;
        }
    };
    scan(s, '(');
    scan([...s].reverse().join(''), ')');
    return best;
};
