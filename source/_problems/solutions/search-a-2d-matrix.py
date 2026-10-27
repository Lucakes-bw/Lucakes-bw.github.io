class Solution:
    def searchMatrix(self, matrix: List[List[int]], target: int) -> bool:
        # 先二分找行（最后一个行首 <= target 的行），再在行内二分
        i = bisect_right([row[0] for row in matrix], target) - 1
        if i < 0:
            return False
        j = bisect_left(matrix[i], target)
        return j < len(matrix[i]) and matrix[i][j] == target
