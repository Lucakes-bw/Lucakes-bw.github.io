class Solution {
    vector<vector<int>> res;
    vector<int> path;
    void go(vector<int>& c, int i, int rest) {
        if (rest == 0) { res.push_back(path); return; }
        for (int j = i; j < (int)c.size() && c[j] <= rest; j++) {
            path.push_back(c[j]);
            go(c, j, rest - c[j]);   // 还可以继续选 c[j]
            path.pop_back();
        }
    }
public:
    vector<vector<int>> combinationSum(vector<int>& candidates, int target) {
        sort(candidates.begin(), candidates.end());
        go(candidates, 0, target);
        return res;
    }
};
