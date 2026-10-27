var minPathSum = function(grid) {
    const n = grid[0].length, f = new Array(n).fill(Infinity);
    f[0] = 0;
    for (const row of grid) {
        f[0] += row[0];
        for (let j = 1; j < n; j++) f[j] = row[j] + Math.min(f[j], f[j - 1]);
    }
    return f[n - 1];
};
