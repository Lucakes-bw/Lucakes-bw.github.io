var numSquares = function(n) {
    const f = new Array(n + 1).fill(Infinity);
    f[0] = 0;
    for (let j = 1; j * j <= n; j++)
        for (let i = j * j; i <= n; i++) f[i] = Math.min(f[i], f[i - j * j] + 1);
    return f[n];
};
