var addTwoNumbers = function(l1, l2) {
    const dummy = new ListNode();
    let t = dummy, carry = 0;
    while (l1 || l2 || carry) {
        let s = carry;
        if (l1) s += l1.val, l1 = l1.next;
        if (l2) s += l2.val, l2 = l2.next;
        t = t.next = new ListNode(s % 10);
        carry = Math.floor(s / 10);
    }
    return dummy.next;
};
