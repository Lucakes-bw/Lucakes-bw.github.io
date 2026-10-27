var coinChange = function(coins, amount) {
    const f = new Array(amount + 1).fill(Infinity);
    f[0] = 0;
    for (const c of coins)
        for (let x = c; x <= amount; x++) f[x] = Math.min(f[x], f[x - c] + 1);
    return f[amount] === Infinity ? -1 : f[amount];
};
