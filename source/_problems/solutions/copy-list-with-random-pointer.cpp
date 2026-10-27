class Solution {
public:
    Node* copyRandomList(Node* head) {
        // 把每个新节点插在原节点后面：A -> A' -> B -> B'
        for (Node* p = head; p; p = p->next->next) {
            Node* c = new Node(p->val);
            c->next = p->next;
            p->next = c;
        }
        for (Node* p = head; p; p = p->next->next)
            if (p->random) p->next->random = p->random->next;
        // 拆开，同时恢复原链表
        Node dummy(0), *t = &dummy;
        for (Node* p = head; p; p = p->next) {
            t = t->next = p->next;
            p->next = p->next->next;
        }
        return dummy.next;
    }
};
