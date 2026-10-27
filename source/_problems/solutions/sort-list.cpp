class Solution {
    ListNode* merge(ListNode* a, ListNode* b) {
        ListNode dummy, *t = &dummy;
        while (a && b) {
            if (a->val <= b->val) t = t->next = a, a = a->next;
            else t = t->next = b, b = b->next;
        }
        t->next = a ? a : b;
        return dummy.next;
    }
public:
    // 自底向上归并：段长 1, 2, 4, ...，O(1) 额外空间
    ListNode* sortList(ListNode* head) {
        int n = 0;
        for (ListNode* p = head; p; p = p->next) n++;
        ListNode dummy(0, head);
        for (int step = 1; step < n; step *= 2) {
            ListNode *tail = &dummy, *cur = dummy.next;
            while (cur) {
                ListNode* a = cur;
                for (int i = 1; i < step && cur->next; i++) cur = cur->next;
                ListNode* b = cur->next;
                cur->next = nullptr;
                cur = b;
                for (int i = 1; i < step && cur && cur->next; i++) cur = cur->next;
                ListNode* rest = nullptr;
                if (cur) rest = cur->next, cur->next = nullptr;
                tail->next = merge(a, b);
                while (tail->next) tail = tail->next;
                cur = rest;
            }
        }
        return dummy.next;
    }
};
