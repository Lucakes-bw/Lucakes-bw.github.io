// 手写双向链表 + 哈希表；head、tail 是哨兵，head.next 是最近使用的
var LRUCache = function(capacity) {
    this.cap = capacity;
    this.map = new Map();
    this.head = { prev: null, next: null };
    this.tail = { prev: this.head, next: null };
    this.head.next = this.tail;
};

LRUCache.prototype._remove = function(node) {
    node.prev.next = node.next;
    node.next.prev = node.prev;
};

LRUCache.prototype._addFront = function(node) {
    node.next = this.head.next;
    node.prev = this.head;
    this.head.next.prev = node;
    this.head.next = node;
};

LRUCache.prototype.get = function(key) {
    const node = this.map.get(key);
    if (!node) return -1;
    this._remove(node);
    this._addFront(node);
    return node.value;
};

LRUCache.prototype.put = function(key, value) {
    let node = this.map.get(key);
    if (node) {
        node.value = value;
        this._remove(node);
    } else {
        node = { key, value };
        this.map.set(key, node);
    }
    this._addFront(node);
    if (this.map.size > this.cap) {
        const last = this.tail.prev;
        this._remove(last);
        this.map.delete(last.key);
    }
};
