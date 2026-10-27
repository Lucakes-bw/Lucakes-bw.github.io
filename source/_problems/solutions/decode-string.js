var decodeString = function(s) {
    const st = [];
    let cur = '', k = 0;
    for (const c of s) {
        if (c >= '0' && c <= '9') k = k * 10 + (+c);
        else if (c === '[') { st.push([cur, k]); cur = ''; k = 0; }
        else if (c === ']') { const [prev, times] = st.pop(); cur = prev + cur.repeat(times); }
        else cur += c;
    }
    return cur;
};
