var longestCommonSubsequence = function(text1, text2) {
    const n = text2.length;
    let prev = new Array(n + 1).fill(0);
    for (const a of text1) {
        const cur = new Array(n + 1).fill(0);
        for (let j = 1; j <= n; j++)
            cur[j] = a === text2[j - 1] ? prev[j - 1] + 1 : Math.max(prev[j], cur[j - 1]);
        prev = cur;
    }
    return prev[n];
};
