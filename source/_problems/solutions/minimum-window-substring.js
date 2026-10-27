var minWindow = function(s, t) {
    const need = new Map();
    for (const c of t) need.set(c, (need.get(c) || 0) + 1);
    let kinds = need.size, start = 0, len = Infinity;
    for (let l = 0, r = 0; r < s.length; r++) {
        if (need.has(s[r])) {
            need.set(s[r], need.get(s[r]) - 1);
            if (need.get(s[r]) === 0) kinds--;
        }
        while (kinds === 0) {
            if (r - l + 1 < len) start = l, len = r - l + 1;
            const c = s[l++];
            if (need.has(c)) {
                if (need.get(c) === 0) kinds++;
                need.set(c, need.get(c) + 1);
            }
        }
    }
    return len === Infinity ? '' : s.substr(start, len);
};
