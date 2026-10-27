class Solution {
public:
    bool canFinish(int numCourses, vector<vector<int>>& prerequisites) {
        vector<vector<int>> g(numCourses);
        vector<int> indeg(numCourses);
        for (auto& p : prerequisites) g[p[1]].push_back(p[0]), indeg[p[0]]++;
        queue<int> q;
        for (int i = 0; i < numCourses; i++) if (!indeg[i]) q.push(i);
        int done = 0;
        while (!q.empty()) {
            int u = q.front(); q.pop();
            done++;
            for (int v : g[u]) if (--indeg[v] == 0) q.push(v);
        }
        return done == numCourses;
    }
};
