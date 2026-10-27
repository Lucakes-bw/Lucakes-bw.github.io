class Solution:
    def topKFrequent(self, nums: List[int], k: int) -> List[int]:
        # 桶排序：buckets[c] 是出现了 c 次的数
        buckets = [[] for _ in range(len(nums) + 1)]
        for x, c in Counter(nums).items():
            buckets[c].append(x)
        res = []
        for c in range(len(nums), 0, -1):
            res += buckets[c]
            if len(res) >= k:
                return res[:k]
        return res
