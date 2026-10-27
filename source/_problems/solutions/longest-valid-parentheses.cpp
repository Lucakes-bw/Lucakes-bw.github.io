class Solution {
public:
    int longestValidParentheses(string s) {
        vector<int> st{-1};   // 栈底：最后一个没被匹配的右括号的下标
        int best = 0;
        for (int i = 0; i < (int)s.size(); i++) {
            if (s[i] == '(') st.push_back(i);
            else {
                st.pop_back();
                if (st.empty()) st.push_back(i);
                else best = max(best, i - st.back());
            }
        }
        return best;
    }
};
