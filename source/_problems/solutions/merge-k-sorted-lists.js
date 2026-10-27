var mergeKLists = function(lists) {
    const merge2 = (a, b) => {
        const dummy = new ListNode();
        let t = dummy;
        while (a && b) {
            if (a.val <= b.val) t = t.next = a, a = a.next;
            else t = t.next = b, b = b.next;
        }
        t.next = a || b;
        return dummy.next;
    };
    // 分治：每轮两两合并，链表数减半
    if (!lists.length) return null;
    while (lists.length > 1) {
        const next = [];
        for (let i = 0; i < lists.length; i += 2) next.push(merge2(lists[i], lists[i + 1] || null));
        lists = next;
    }
    return lists[0];
};
