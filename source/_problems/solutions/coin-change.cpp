class Solution {
public:
    int coinChange(vector<int>& coins, int amount) {
        vector<int> f(amount + 1, INT_MAX);
        f[0] = 0;
        for (int x = 1; x <= amount; x++)
            for (int c : coins)
                if (c <= x && f[x - c] != INT_MAX) f[x] = min(f[x], f[x - c] + 1);
        return f[amount] == INT_MAX ? -1 : f[amount];
    }
};
