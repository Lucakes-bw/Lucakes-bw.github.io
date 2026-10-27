var reverseKGroup = function(head, k) {
    const dummy = new ListNode(0, head);
    let tail = dummy;
    while (true) {
        let p = tail.next;
        for (let i = 0; i < k; i++) {
            if (!p) return dummy.next;
            p = p.next;
        }
        const first = tail.next;
        let prev = p, cur = first;
        while (cur !== p) {
            const nxt = cur.next;
            cur.next = prev;
            prev = cur, cur = nxt;
        }
        tail.next = prev;
        tail = first;
    }
};
