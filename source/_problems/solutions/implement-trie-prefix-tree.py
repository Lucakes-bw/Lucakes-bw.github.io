class Trie:

    def __init__(self):
        self.root = {}   # 每个节点是 {字符: 子节点}，'$' 表示有单词在这里结束

    def insert(self, word: str) -> None:
        p = self.root
        for c in word:
            p = p.setdefault(c, {})
        p['$'] = True

    def _walk(self, s):
        p = self.root
        for c in s:
            if c not in p:
                return None
            p = p[c]
        return p

    def search(self, word: str) -> bool:
        p = self._walk(word)
        return p is not None and '$' in p

    def startsWith(self, prefix: str) -> bool:
        return self._walk(prefix) is not None
