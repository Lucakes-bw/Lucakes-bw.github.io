class Solution {
public:
    int maxProfit(vector<int>& prices) {
        int low = INT_MAX, best = 0;
        for (int p : prices) {
            low = min(low, p);
            best = max(best, p - low);
        }
        return best;
    }
};
