class LRUCache:

    def __init__(self, capacity: int):
        self.cap = capacity
        self.items = OrderedDict()   # 越靠后越新

    def get(self, key: int) -> int:
        if key not in self.items:
            return -1
        self.items.move_to_end(key)
        return self.items[key]

    def put(self, key: int, value: int) -> None:
        self.items[key] = value
        self.items.move_to_end(key)
        if len(self.items) > self.cap:
            self.items.popitem(last=False)
