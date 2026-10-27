class Solution:
    def sortColors(self, nums: List[int]) -> None:
        # 计数排序：数出 0、1、2 各有几个，再依次写回
        cnt = [0, 0, 0]
        for x in nums:
            cnt[x] += 1
        k = 0
        for c in range(3):
            for _ in range(cnt[c]):
                nums[k] = c
                k += 1
