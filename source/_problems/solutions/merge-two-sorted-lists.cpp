class Solution {
public:
    ListNode* mergeTwoLists(ListNode* list1, ListNode* list2) {
        ListNode dummy, *t = &dummy;
        while (list1 && list2) {
            ListNode*& p = list1->val <= list2->val ? list1 : list2;
            t = t->next = p;
            p = p->next;
        }
        t->next = list1 ? list1 : list2;
        return dummy.next;
    }
};
