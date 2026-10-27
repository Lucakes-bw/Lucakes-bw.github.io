var longestPalindrome = function(s) {
    let start = 0, len = 1;
    const expand = (l, r) => {
        while (l >= 0 && r < s.length && s[l] === s[r]) l--, r++;
        if (r - l - 1 > len) start = l + 1, len = r - l - 1;
    };
    for (let i = 0; i < s.length; i++) {
        expand(i, i);
        expand(i, i + 1);
    }
    return s.substr(start, len);
};
