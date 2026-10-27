var letterCombinations = function(digits) {
    if (!digits) return [];
    const keys = ['', '', 'abc', 'def', 'ghi', 'jkl', 'mno', 'pqrs', 'tuv', 'wxyz'];
    let res = [''];
    for (const d of digits) res = res.flatMap(s => [...keys[d]].map(c => s + c));
    return res;
};
