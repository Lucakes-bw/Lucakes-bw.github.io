var mergeTwoLists = function(list1, list2) {
    const dummy = new ListNode();
    let t = dummy;
    while (list1 && list2) {
        if (list1.val <= list2.val) t = t.next = list1, list1 = list1.next;
        else t = t.next = list2, list2 = list2.next;
    }
    t.next = list1 || list2;
    return dummy.next;
};
