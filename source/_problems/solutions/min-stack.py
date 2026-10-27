class MinStack:

    def __init__(self):
        self.st = []   # 每一项是 (值, 压入它之后的最小值)

    def push(self, val: int) -> None:
        self.st.append((val, min(val, self.st[-1][1]) if self.st else val))

    def pop(self) -> None:
        self.st.pop()

    def top(self) -> int:
        return self.st[-1][0]

    def getMin(self) -> int:
        return self.st[-1][1]
