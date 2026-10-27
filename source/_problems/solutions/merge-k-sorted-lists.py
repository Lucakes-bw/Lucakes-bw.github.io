class Solution:
    def mergeKLists(self, lists: List[Optional[ListNode]]) -> Optional[ListNode]:
        # 下标 i 放进元组，值相同时不会去比较 ListNode
        h = [(l.val, i, l) for i, l in enumerate(lists) if l]
        heapify(h)
        dummy = t = ListNode()
        while h:
            _, i, p = heappop(h)
            t.next = p
            t = p
            if p.next:
                heappush(h, (p.next.val, i, p.next))
        return dummy.next
