// 数值范围只有 [-10^5, 10^5]：用计数数组 + 树状数组，按排名找第 k 小
const OFF = 100001, N = 200003;
var MedianFinder = function() {
    this.bit = new Int32Array(N + 1);
    this.n = 0;
};

MedianFinder.prototype.addNum = function(num) {
    for (let i = num + OFF; i <= N; i += i & -i) this.bit[i]++;
    this.n++;
};

// 第 k 小（k 从 1 开始）：在树状数组上倍增
MedianFinder.prototype._kth = function(k) {
    let pos = 0;
    for (let step = 1 << 17; step; step >>= 1)
        if (pos + step <= N && this.bit[pos + step] < k) pos += step, k -= this.bit[pos];
    return pos + 1 - OFF;
};

MedianFinder.prototype.findMedian = function() {
    const n = this.n;
    return n % 2 ? this._kth((n + 1) / 2) : (this._kth(n / 2) + this._kth(n / 2 + 1)) / 2;
};
