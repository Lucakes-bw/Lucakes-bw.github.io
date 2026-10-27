class Solution {
public:
    ListNode* mergeKLists(vector<ListNode*>& lists) {
        auto cmp = [](ListNode* a, ListNode* b) { return a->val > b->val; };
        priority_queue<ListNode*, vector<ListNode*>, decltype(cmp)> pq(cmp);
        for (ListNode* l : lists) if (l) pq.push(l);
        ListNode dummy, *t = &dummy;
        while (!pq.empty()) {
            ListNode* p = pq.top(); pq.pop();
            t = t->next = p;
            if (p->next) pq.push(p->next);
        }
        return dummy.next;
    }
};
