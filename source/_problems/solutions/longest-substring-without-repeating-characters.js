var lengthOfLongestSubstring = function(s) {
    const win = new Set();
    let l = 0, best = 0;
    for (let r = 0; r < s.length; r++) {
        while (win.has(s[r])) win.delete(s[l++]);
        win.add(s[r]);
        best = Math.max(best, r - l + 1);
    }
    return best;
};
