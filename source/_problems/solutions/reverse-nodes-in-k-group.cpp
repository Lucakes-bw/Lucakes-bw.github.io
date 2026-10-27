class Solution {
public:
    ListNode* reverseKGroup(ListNode* head, int k) {
        ListNode dummy(0, head), *tail = &dummy;   // tail：上一组翻转后的尾巴
        while (true) {
            ListNode* p = tail->next;
            for (int i = 0; i < k; i++) {
                if (!p) return dummy.next;
                p = p->next;
            }
            // 翻转 tail->next 开始的 k 个节点，p 是下一组的开头
            ListNode *first = tail->next, *prev = p, *cur = first;
            while (cur != p) {
                ListNode* nxt = cur->next;
                cur->next = prev;
                prev = cur, cur = nxt;
            }
            tail->next = prev;
            tail = first;
        }
    }
};
