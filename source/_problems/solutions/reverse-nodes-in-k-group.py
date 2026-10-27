class Solution:
    def reverseKGroup(self, head: Optional[ListNode], k: int) -> Optional[ListNode]:
        p = head
        for _ in range(k):
            if not p:
                return head
            p = p.next
        prev, cur = self.reverseKGroup(p, k), head
        while cur is not p:
            cur.next, prev, cur = prev, cur, cur.next
        return prev
