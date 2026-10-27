class Solution:
    def trap(self, height: List[int]) -> int:
        # 单调栈：遇到比栈顶高的柱子，就按「层」结算中间的凹槽
        st, res = [], 0
        for i, h in enumerate(height):
            while st and height[st[-1]] < h:
                bottom = height[st.pop()]
                if not st:
                    break
                l = st[-1]
                res += (min(height[l], h) - bottom) * (i - l - 1)
            st.append(i)
        return res
