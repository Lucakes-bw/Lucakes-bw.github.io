class Solution {
public:
    string decodeString(string s) {
        vector<pair<string, int>> st;   // 进入 [ 之前已经拼好的串、重复次数
        string cur;
        int k = 0;
        for (char c : s) {
            if (isdigit(c)) k = k * 10 + (c - '0');
            else if (c == '[') { st.push_back({cur, k}); cur.clear(); k = 0; }
            else if (c == ']') {
                auto [prev, times] = st.back(); st.pop_back();
                for (int i = 0; i < times; i++) prev += cur;
                cur = move(prev);
            } else cur += c;
        }
        return cur;
    }
};
