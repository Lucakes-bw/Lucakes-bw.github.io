var isPalindrome = function(head) {
    let slow = head, fast = head, prev = null;
    // 快指针往前走的同时，把前半段反转
    while (fast && fast.next) {
        fast = fast.next.next;
        const nxt = slow.next;
        slow.next = prev;
        prev = slow, slow = nxt;
    }
    if (fast) slow = slow.next;   // 奇数个节点，跳过正中间
    for (; slow; slow = slow.next, prev = prev.next)
        if (slow.val !== prev.val) return false;
    return true;
};
