class Solution:
    def searchMatrix(self, matrix: List[List[int]], target: int) -> bool:
        # 每一行二分，O(m log n)
        for row in matrix:
            k = bisect_left(row, target)
            if k < len(row) and row[k] == target:
                return True
        return False
