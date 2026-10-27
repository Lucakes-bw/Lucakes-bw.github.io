var removeNthFromEnd = function(head, n) {
    // 先数长度，再走到要删除节点的前一个
    let len = 0;
    for (let p = head; p; p = p.next) len++;
    const dummy = new ListNode(0, head);
    let p = dummy;
    for (let i = 0; i < len - n; i++) p = p.next;
    p.next = p.next.next;
    return dummy.next;
};
