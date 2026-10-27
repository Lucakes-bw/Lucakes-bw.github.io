class Solution {
public:
    int climbStairs(int n) {
        int a = 1, b = 1;   // f(i - 1), f(i)
        for (int i = 2; i <= n; i++) {
            int c = a + b;
            a = b, b = c;
        }
        return b;
    }
};
