class Solution {
public:
    bool searchMatrix(vector<vector<int>>& matrix, int target) {
        int m = matrix.size(), n = matrix[0].size(), l = 0, r = m * n - 1;
        while (l <= r) {
            int mid = l + (r - l) / 2, v = matrix[mid / n][mid % n];
            if (v == target) return true;
            if (v < target) l = mid + 1;
            else r = mid - 1;
        }
        return false;
    }
};
