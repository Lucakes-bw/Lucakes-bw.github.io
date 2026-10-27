class Solution:
    def sortList(self, head: Optional[ListNode]) -> Optional[ListNode]:
        if not head or not head.next:
            return head
        slow, fast = head, head.next
        while fast and fast.next:
            slow, fast = slow.next, fast.next.next
        mid, slow.next = slow.next, None
        a, b = self.sortList(head), self.sortList(mid)
        dummy = t = ListNode()
        while a and b:
            if a.val <= b.val:
                t.next, a = a, a.next
            else:
                t.next, b = b, b.next
            t = t.next
        t.next = a or b
        return dummy.next
