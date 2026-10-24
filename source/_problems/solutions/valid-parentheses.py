class Solution:
    def isValid(self, s: str) -> bool:
        pair = {')': '(', ']': '[', '}': '{'}
        st = []
        for c in s:
            if c in pair:
                if not st or st.pop() != pair[c]:
                    return False
            else:
                st.append(c)
        return not st
