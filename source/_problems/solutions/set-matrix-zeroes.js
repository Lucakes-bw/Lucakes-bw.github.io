var setZeroes = function(matrix) {
    const m = matrix.length, n = matrix[0].length;
    const rows = new Array(m).fill(false), cols = new Array(n).fill(false);
    for (let i = 0; i < m; i++)
        for (let j = 0; j < n; j++)
            if (matrix[i][j] === 0) rows[i] = cols[j] = true;
    for (let i = 0; i < m; i++)
        for (let j = 0; j < n; j++)
            if (rows[i] || cols[j]) matrix[i][j] = 0;
};
