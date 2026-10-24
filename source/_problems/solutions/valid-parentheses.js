var isValid = function(s) {
    const pair = { ')': '(', ']': '[', '}': '{' };
    const st = [];
    for (const c of s) {
        if (pair[c]) {
            if (st.pop() !== pair[c]) return false;
        } else st.push(c);
    }
    return st.length === 0;
};
