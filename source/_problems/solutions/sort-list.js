var sortList = function(head) {
    if (!head || !head.next) return head;
    let slow = head, fast = head.next;
    while (fast && fast.next) slow = slow.next, fast = fast.next.next;
    const mid = slow.next;
    slow.next = null;
    let a = sortList(head), b = sortList(mid);
    const dummy = new ListNode();
    let t = dummy;
    while (a && b) {
        if (a.val <= b.val) t = t.next = a, a = a.next;
        else t = t.next = b, b = b.next;
    }
    t.next = a || b;
    return dummy.next;
};
