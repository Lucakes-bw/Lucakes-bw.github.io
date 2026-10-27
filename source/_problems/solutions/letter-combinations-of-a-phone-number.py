class Solution:
    def letterCombinations(self, digits: str) -> List[str]:
        if not digits:
            return []
        keys = {'2': 'abc', '3': 'def', '4': 'ghi', '5': 'jkl', '6': 'mno', '7': 'pqrs', '8': 'tuv', '9': 'wxyz'}
        res = ['']
        for d in digits:
            res = [s + c for s in res for c in keys[d]]
        return res
