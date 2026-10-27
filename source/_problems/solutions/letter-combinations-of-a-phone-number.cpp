class Solution {
public:
    vector<string> letterCombinations(string digits) {
        if (digits.empty()) return {};
        static const string keys[] = {"", "", "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz"};
        vector<string> res;
        string path;
        function<void(int)> go = [&](int i) {
            if (i == (int)digits.size()) { res.push_back(path); return; }
            for (char c : keys[digits[i] - '0']) {
                path.push_back(c);
                go(i + 1);
                path.pop_back();
            }
        };
        go(0);
        return res;
    }
};
