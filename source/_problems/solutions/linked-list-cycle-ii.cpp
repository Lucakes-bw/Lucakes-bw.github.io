class Solution {
public:
    ListNode *detectCycle(ListNode *head) {
        ListNode *slow = head, *fast = head;
        while (fast && fast->next) {
            slow = slow->next;
            fast = fast->next->next;
            if (slow == fast) {
                // 头到入口的距离 = 相遇点再走到入口的距离（差若干整圈）
                for (ListNode* p = head; p != slow; p = p->next) slow = slow->next;
                return slow;
            }
        }
        return nullptr;
    }
};
