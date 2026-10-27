class MedianFinder {
    priority_queue<int> lo;                              // 较小的一半，大根堆
    priority_queue<int, vector<int>, greater<int>> hi;   // 较大的一半，小根堆
public:
    MedianFinder() {}

    void addNum(int num) {
        lo.push(num);
        hi.push(lo.top()); lo.pop();
        if (hi.size() > lo.size()) { lo.push(hi.top()); hi.pop(); }   // 保持 lo 不比 hi 少
    }

    double findMedian() {
        return lo.size() > hi.size() ? lo.top() : (lo.top() + (double)hi.top()) / 2;
    }
};
