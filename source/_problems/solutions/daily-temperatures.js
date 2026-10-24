var dailyTemperatures = function(temperatures) {
    const ans = new Array(temperatures.length).fill(0), st = [];
    temperatures.forEach((t, i) => {
        while (st.length && temperatures[st[st.length - 1]] < t) {
            const j = st.pop();
            ans[j] = i - j;
        }
        st.push(i);
    });
    return ans;
};
