var solveNQueens = function(n) {
    const res = [], pos = [];
    // 用位掩码记录被占用的列和两条对角线
    const go = (cols, d1, d2) => {
        if (pos.length === n) {
            res.push(pos.map(c => '.'.repeat(c) + 'Q' + '.'.repeat(n - 1 - c)));
            return;
        }
        let free = ~(cols | d1 | d2) & ((1 << n) - 1);
        while (free) {
            const bit = free & -free;
            free ^= bit;
            pos.push(31 - Math.clz32(bit));
            go(cols | bit, (d1 | bit) << 1, (d2 | bit) >> 1);
            pos.pop();
        }
    };
    go(0, 0, 0);
    return res;
};
