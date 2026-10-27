class Solution {
public:
    vector<int> spiralOrder(vector<vector<int>>& matrix) {
        int top = 0, bottom = matrix.size() - 1, left = 0, right = matrix[0].size() - 1;
        vector<int> res;
        while (top <= bottom && left <= right) {
            for (int j = left; j <= right; j++) res.push_back(matrix[top][j]);
            for (int i = top + 1; i <= bottom; i++) res.push_back(matrix[i][right]);
            if (top < bottom && left < right) {
                for (int j = right - 1; j > left; j--) res.push_back(matrix[bottom][j]);
                for (int i = bottom; i > top; i--) res.push_back(matrix[i][left]);
            }
            top++, bottom--, left++, right--;
        }
        return res;
    }
};
