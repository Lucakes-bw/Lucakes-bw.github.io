var searchMatrix = function(matrix, target) {
    const m = matrix.length, n = matrix[0].length;
    let l = 0, r = m * n - 1;
    while (l <= r) {
        const mid = (l + r) >> 1, v = matrix[Math.floor(mid / n)][mid % n];
        if (v === target) return true;
        if (v < target) l = mid + 1;
        else r = mid - 1;
    }
    return false;
};
