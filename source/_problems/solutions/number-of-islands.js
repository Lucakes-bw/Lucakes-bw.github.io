var numIslands = function(grid) {
    // 并查集
    const m = grid.length, n = grid[0].length, parent = [];
    const find = x => (parent[x] === x ? x : (parent[x] = find(parent[x])));
    let cnt = 0;
    for (let i = 0; i < m * n; i++) parent[i] = i;
    for (let i = 0; i < m; i++) {
        for (let j = 0; j < n; j++) {
            if (grid[i][j] !== '1') continue;
            cnt++;
            for (const [a, b] of [[i - 1, j], [i, j - 1]]) {
                if (a < 0 || b < 0 || grid[a][b] !== '1') continue;
                const p = find(a * n + b), q = find(i * n + j);
                if (p !== q) { parent[p] = q; cnt--; }
            }
        }
    }
    return cnt;
};
