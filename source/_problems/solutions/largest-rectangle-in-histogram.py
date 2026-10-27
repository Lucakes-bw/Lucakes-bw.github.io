class Solution:
    def largestRectangleArea(self, heights: List[int]) -> int:
        n = len(heights)
        left, right, st = [-1] * n, [n] * n, []   # 左右两边第一个更矮的位置
        for i, h in enumerate(heights):
            while st and heights[st[-1]] >= h:
                right[st.pop()] = i
            left[i] = st[-1] if st else -1
            st.append(i)
        return max(h * (right[i] - left[i] - 1) for i, h in enumerate(heights))
