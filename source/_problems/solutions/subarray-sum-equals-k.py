class Solution:
    def subarraySum(self, nums: List[int], k: int) -> int:
        cnt = Counter({0: 1})
        pre = res = 0
        for x in nums:
            pre += x
            res += cnt[pre - k]
            cnt[pre] += 1
        return res
