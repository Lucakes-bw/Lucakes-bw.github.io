class Solution:
    def dailyTemperatures(self, temperatures: List[int]) -> List[int]:
        ans = [0] * len(temperatures)
        st = []
        for i, t in enumerate(temperatures):
            while st and temperatures[st[-1]] < t:
                j = st.pop()
                ans[j] = i - j
            st.append(i)
        return ans
