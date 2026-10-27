var maxSlidingWindow = function(nums, k) {
    const q = [], res = [];   // q 当双端队列用，head 是队头位置
    let head = 0;
    for (let i = 0; i < nums.length; i++) {
        while (q.length > head && nums[q[q.length - 1]] <= nums[i]) q.pop();
        q.push(i);
        if (q[head] <= i - k) head++;
        if (i >= k - 1) res.push(nums[q[head]]);
    }
    return res;
};
