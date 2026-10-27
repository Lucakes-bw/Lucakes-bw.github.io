class Solution {
public:
    bool isPalindrome(ListNode* head) {
        ListNode *slow = head, *fast = head;
        while (fast && fast->next) slow = slow->next, fast = fast->next->next;
        ListNode* prev = nullptr;   // 反转后半段
        while (slow) {
            ListNode* nxt = slow->next;
            slow->next = prev;
            prev = slow, slow = nxt;
        }
        for (ListNode *a = head, *b = prev; b; a = a->next, b = b->next)
            if (a->val != b->val) return false;
        return true;
    }
};
