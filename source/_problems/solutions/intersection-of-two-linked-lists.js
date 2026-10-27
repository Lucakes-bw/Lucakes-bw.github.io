var getIntersectionNode = function(headA, headB) {
    const seen = new Set();
    for (let p = headA; p; p = p.next) seen.add(p);
    for (let p = headB; p; p = p.next) if (seen.has(p)) return p;
    return null;
};
