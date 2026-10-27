var uniquePaths = function(m, n) {
    // C(m + n - 2, m - 1)，边乘边除，每一步结果都是整数
    let res = 1;
    for (let i = 1; i < m; i++) res = res * (n - 1 + i) / i;
    return Math.round(res);
};
