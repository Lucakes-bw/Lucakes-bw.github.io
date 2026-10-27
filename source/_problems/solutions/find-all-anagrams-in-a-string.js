var findAnagrams = function(s, p) {
    const m = p.length, cnt = new Array(26).fill(0), res = [];
    for (const c of p) cnt[c.charCodeAt(0) - 97]++;
    // diff：窗口和 p 计数不同的字母个数
    let diff = cnt.filter(x => x).length;
    const add = (c, d) => {
        const k = c.charCodeAt(0) - 97;
        if (cnt[k] === 0) diff++;
        cnt[k] -= d;
        if (cnt[k] === 0) diff--;
    };
    for (let i = 0; i < s.length; i++) {
        add(s[i], 1);
        if (i >= m) add(s[i - m], -1);
        if (i >= m - 1 && diff === 0) res.push(i - m + 1);
    }
    return res;
};
