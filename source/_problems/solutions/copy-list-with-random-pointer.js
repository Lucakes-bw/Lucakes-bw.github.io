var copyRandomList = function(head) {
    const copy = new Map([[null, null]]);
    for (let p = head; p; p = p.next) copy.set(p, new _Node(p.val, null, null));
    for (let p = head; p; p = p.next) {
        copy.get(p).next = copy.get(p.next);
        copy.get(p).random = copy.get(p.random);
    }
    return copy.get(head);
};
