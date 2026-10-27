var orangesRotting = function(grid) {
    const m = grid.length, n = grid[0].length;
    let q = [], fresh = 0, minutes = 0;
    for (let i = 0; i < m; i++)
        for (let j = 0; j < n; j++) {
            if (grid[i][j] === 2) q.push([i, j]);
            else if (grid[i][j] === 1) fresh++;
        }
    while (fresh && q.length) {
        minutes++;
        const next = [];
        for (const [i, j] of q)
            for (const [x, y] of [[i + 1, j], [i - 1, j], [i, j + 1], [i, j - 1]])
                if (x >= 0 && x < m && y >= 0 && y < n && grid[x][y] === 1) {
                    grid[x][y] = 2;
                    fresh--;
                    next.push([x, y]);
                }
        q = next;
    }
    return fresh ? -1 : minutes;
};
