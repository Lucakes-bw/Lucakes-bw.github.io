class Solution {
public:
    ListNode *getIntersectionNode(ListNode *headA, ListNode *headB) {
        ListNode *a = headA, *b = headB;
        while (a != b) {   // 都走 m + n 步：要么在交点相遇，要么同时为 nullptr
            a = a ? a->next : headB;
            b = b ? b->next : headA;
        }
        return a;
    }
};
