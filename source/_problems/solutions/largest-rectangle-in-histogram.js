var largestRectangleArea = function(heights) {
    const st = [];
    let best = 0;
    for (let i = 0; i <= heights.length; i++) {
        const h = i === heights.length ? 0 : heights[i];
        while (st.length && heights[st[st.length - 1]] >= h) {
            const top = heights[st.pop()];
            const left = st.length ? st[st.length - 1] : -1;
            best = Math.max(best, top * (i - left - 1));
        }
        st.push(i);
    }
    return best;
};
