var spiralOrder = function(matrix) {
    let top = 0, bottom = matrix.length - 1, left = 0, right = matrix[0].length - 1;
    const res = [];
    while (top <= bottom && left <= right) {
        for (let j = left; j <= right; j++) res.push(matrix[top][j]);
        for (let i = top + 1; i <= bottom; i++) res.push(matrix[i][right]);
        if (top < bottom && left < right) {
            for (let j = right - 1; j > left; j--) res.push(matrix[bottom][j]);
            for (let i = bottom; i > top; i--) res.push(matrix[i][left]);
        }
        top++, bottom--, left++, right--;
    }
    return res;
};
