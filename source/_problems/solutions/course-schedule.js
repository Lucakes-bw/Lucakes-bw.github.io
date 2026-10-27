var canFinish = function(numCourses, prerequisites) {
    const g = Array.from({ length: numCourses }, () => []), indeg = new Array(numCourses).fill(0);
    for (const [a, b] of prerequisites) g[b].push(a), indeg[a]++;
    const q = [];
    for (let i = 0; i < numCourses; i++) if (!indeg[i]) q.push(i);
    for (let h = 0; h < q.length; h++)
        for (const v of g[q[h]]) if (--indeg[v] === 0) q.push(v);
    return q.length === numCourses;
};
