class Solution:
    def exist(self, board: List[List[str]], word: str) -> bool:
        m, n = len(board), len(board[0])
        have = Counter(c for row in board for c in row)
        need = Counter(word)
        if any(have[c] < k for c, k in need.items()):
            return False
        if have[word[0]] > have[word[-1]]:
            word = word[::-1]
        def dfs(i, j, k):
            if not (0 <= i < m and 0 <= j < n) or board[i][j] != word[k]:
                return False
            if k == len(word) - 1:
                return True
            c, board[i][j] = board[i][j], '#'
            ok = dfs(i + 1, j, k + 1) or dfs(i - 1, j, k + 1) or dfs(i, j + 1, k + 1) or dfs(i, j - 1, k + 1)
            board[i][j] = c
            return ok
        return any(dfs(i, j, 0) for i in range(m) for j in range(n))
