var minDistance = function(word1, word2) {
    const m = word1.length, n = word2.length;
    const f = Array.from({ length: m + 1 }, (_, i) => Array.from({ length: n + 1 }, (_, j) => i === 0 ? j : j === 0 ? i : 0));
    for (let i = 1; i <= m; i++)
        for (let j = 1; j <= n; j++)
            f[i][j] = word1[i - 1] === word2[j - 1] ? f[i - 1][j - 1]
                : 1 + Math.min(f[i - 1][j], f[i][j - 1], f[i - 1][j - 1]);
    return f[m][n];
};
