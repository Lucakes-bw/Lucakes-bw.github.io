var findKthLargest = function(nums, k) {
    // 快速选择：找排序后下标为 n - k 的元素
    const a = nums.slice(), want = a.length - k;
    let l = 0, r = a.length - 1;
    while (true) {
        const p = a[(l + r) >> 1];
        let i = l, j = r;
        while (i <= j) {
            while (a[i] < p) i++;
            while (a[j] > p) j--;
            if (i <= j) { [a[i], a[j]] = [a[j], a[i]]; i++; j--; }
        }
        if (want <= j) r = j;
        else if (want >= i) l = i;
        else return a[want];
    }
};
