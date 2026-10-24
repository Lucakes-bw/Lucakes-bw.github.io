class Solution {
public:
    vector<int> dailyTemperatures(vector<int>& t) {
        vector<int> ans(t.size()), st;
        for (int i = 0; i < (int)t.size(); i++) {
            while (!st.empty() && t[st.back()] < t[i]) { ans[st.back()] = i - st.back(); st.pop_back(); }
            st.push_back(i);
        }
        return ans;
    }
};
