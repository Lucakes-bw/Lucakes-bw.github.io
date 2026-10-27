class Solution {
public:
    int largestRectangleArea(vector<int>& heights) {
        heights.push_back(0);   // 哨兵：最后把栈里剩下的都结算掉
        vector<int> st;         // 下标，高度单调递增
        int best = 0;
        for (int i = 0; i < (int)heights.size(); i++) {
            while (!st.empty() && heights[st.back()] >= heights[i]) {
                int h = heights[st.back()]; st.pop_back();
                int left = st.empty() ? -1 : st.back();
                best = max(best, h * (i - left - 1));
            }
            st.push_back(i);
        }
        return best;
    }
};
