var detectCycle = function(head) {
    let slow = head, fast = head;
    while (fast && fast.next) {
        slow = slow.next;
        fast = fast.next.next;
        if (slow === fast) {
            for (let p = head; p !== slow; p = p.next) slow = slow.next;
            return slow;
        }
    }
    return null;
};
