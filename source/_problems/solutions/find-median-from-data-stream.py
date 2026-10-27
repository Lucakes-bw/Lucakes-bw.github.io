class MedianFinder:

    def __init__(self):
        self.lo = []   # 较小的一半，存相反数当大根堆
        self.hi = []   # 较大的一半

    def addNum(self, num: int) -> None:
        heappush(self.hi, -heappushpop(self.lo, -num))
        if len(self.hi) > len(self.lo):
            heappush(self.lo, -heappop(self.hi))

    def findMedian(self) -> float:
        if len(self.lo) > len(self.hi):
            return float(-self.lo[0])
        return (-self.lo[0] + self.hi[0]) / 2
