var wordBreak = function(s, wordDict) {
    // 记忆化搜索：从位置 i 开始的后缀能否拆分
    const memo = new Map();
    const go = i => {
        if (i === s.length) return true;
        if (memo.has(i)) return memo.get(i);
        const ok = wordDict.some(w => s.startsWith(w, i) && go(i + w.length));
        memo.set(i, ok);
        return ok;
    };
    return go(0);
};
