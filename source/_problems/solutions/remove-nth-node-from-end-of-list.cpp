class Solution {
public:
    ListNode* removeNthFromEnd(ListNode* head, int n) {
        ListNode dummy(0, head), *fast = head, *slow = &dummy;
        while (n--) fast = fast->next;
        while (fast) fast = fast->next, slow = slow->next;
        slow->next = slow->next->next;
        return dummy.next;
    }
};
