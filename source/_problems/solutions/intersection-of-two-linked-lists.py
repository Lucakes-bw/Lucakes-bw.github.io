class Solution:
    def getIntersectionNode(self, headA: ListNode, headB: ListNode) -> Optional[ListNode]:
        # 先算长度，让长的那条先走差值步，再一起走
        def length(h):
            n = 0
            while h:
                n, h = n + 1, h.next
            return n
        m, n = length(headA), length(headB)
        a, b = headA, headB
        for _ in range(m - n):
            a = a.next
        for _ in range(n - m):
            b = b.next
        while a is not b:
            a, b = a.next, b.next
        return a
