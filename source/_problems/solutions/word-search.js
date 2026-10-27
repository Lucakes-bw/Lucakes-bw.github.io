var exist = function(board, word) {
    const m = board.length, n = board[0].length, cnt = {};
    for (const row of board) for (const c of row) cnt[c] = (cnt[c] || 0) + 1;
    for (const c of word) if (!cnt[c]--) return false;
    if (cnt[word[word.length - 1]] < cnt[word[0]]) word = [...word].reverse().join('');
    const dfs = (i, j, k) => {
        if (i < 0 || j < 0 || i >= m || j >= n || board[i][j] !== word[k]) return false;
        if (k === word.length - 1) return true;
        const c = board[i][j];
        board[i][j] = '#';
        const ok = dfs(i + 1, j, k + 1) || dfs(i - 1, j, k + 1) || dfs(i, j + 1, k + 1) || dfs(i, j - 1, k + 1);
        board[i][j] = c;
        return ok;
    };
    for (let i = 0; i < m; i++)
        for (let j = 0; j < n; j++)
            if (dfs(i, j, 0)) return true;
    return false;
};
