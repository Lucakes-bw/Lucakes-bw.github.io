class Solution:
    def maxSlidingWindow(self, nums: List[int], k: int) -> List[int]:
        # 大根堆（存负数），堆顶滑出窗口时才弹出
        h = [(-nums[i], i) for i in range(k)]
        heapify(h)
        res = [-h[0][0]]
        for i in range(k, len(nums)):
            heappush(h, (-nums[i], i))
            while h[0][1] <= i - k:
                heappop(h)
            res.append(-h[0][0])
        return res
