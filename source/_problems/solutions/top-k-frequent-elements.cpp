class Solution {
public:
    vector<int> topKFrequent(vector<int>& nums, int k) {
        unordered_map<int, int> cnt;
        for (int x : nums) cnt[x]++;
        // 小根堆，按次数排序，只保留 k 个
        priority_queue<pair<int, int>, vector<pair<int, int>>, greater<>> pq;
        for (auto [x, c] : cnt) {
            pq.push({c, x});
            if ((int)pq.size() > k) pq.pop();
        }
        vector<int> res;
        while (!pq.empty()) res.push_back(pq.top().second), pq.pop();
        return res;
    }
};
