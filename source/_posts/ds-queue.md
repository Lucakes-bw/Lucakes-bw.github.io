---
title: 队列
date: 2026-10-23 20:40:00
updated: 2026-10-23 20:40:00
categories:
  - 数据结构
  - 栈和队列
tags:
  - 数据结构
  - 队列
order: 2
description: 队列是先进先出的线性表。本文先实测「出队时移动所有元素」的朴素实现有多慢，然后实现循环队列，用逐步演示展示 front、rear 回绕和判断队满的方法，再实现链式队列，最后介绍双端队列和队列的典型应用。
top_group_list: true
disableNunjucks: true
viz: [ds-queue]
---

> **本文目标**：理解队列的先进先出特性；理解为什么数组实现的队列要做成循环的，掌握队空和队满的判断；会实现链式队列，注意队列变空时的处理；了解双端队列和队列的应用。
>
> **前置知识**：《顺序表》、《单链表》、《栈》。

## 1. 什么是队列

**队列**（queue）只允许在一端插入、在另一端删除。插入的一端叫**队尾**（rear），删除的一端叫**队头**（front）。插入叫**入队**（enqueue），删除叫**出队**（dequeue）。

就像排队买票：新来的人站在队尾，排在最前面的人先买到票离开。所以**先进入的先出来**，叫**先进先出**（FIFO，First In First Out）。

打印机按顺序处理打印任务、服务器按到达顺序处理请求、键盘按顺序缓存按键，都是队列。

## 2. 为什么要「循环」

用数组实现队列，最直接的想法是：队头固定在 `data[0]`，出队时把后面所有元素向前移一位。但这样每次出队都是 O(n)。实测依次出队 n 个元素（`-O2`）：

```
n = 10000   移动元素      9.8 ms，循环队列 0.077 ms
n = 100000  移动元素    294.0 ms，循环队列 0.258 ms
```

10 万个元素时慢了 1000 多倍。

改进的思路是**不移动元素，而是移动队头的位置**：用 `front` 记录队头的下标，出队时 `front` 加 1。但这样 `front` 和 `rear` 都只会往后走，数组前面空出来的位置永远用不上，很快 `rear` 就到达了数组末尾，明明还有空间却无法入队，这叫**假溢出**。

解决方法是让下标**走到末尾后回到 0**，把数组想象成首尾相连的一个环。这就是**循环队列**。实现的关键只有一个式子：

```c
index = (index + 1) % capacity;
```

## 3. 循环队列

```c
typedef struct {
    int* data;
    size_t capacity;                            // 数组长度，最多存放 capacity - 1 个元素
    size_t front;                               // 队头元素的下标
    size_t rear;                                // 下一个元素要放的位置
} Queue;

int queueInit(Queue* q, size_t capacity)
{
    q->data = malloc(capacity * sizeof(int));
    if (q->data == NULL) return 0;
    q->capacity = capacity;
    q->front = q->rear = 0;
    return 1;
}

int isEmpty(const Queue* q) { return q->front == q->rear; }
int isFull(const Queue* q) { return (q->rear + 1) % q->capacity == q->front; }
size_t queueSize(const Queue* q) { return (q->rear + q->capacity - q->front) % q->capacity; }

int enqueue(Queue* q, int value)
{
    if (isFull(q)) return 0;
    q->data[q->rear] = value;
    q->rear = (q->rear + 1) % q->capacity;      // 走到末尾后回到 0
    return 1;
}

int dequeue(Queue* q, int* out)
{
    if (isEmpty(q)) return 0;
    *out = q->data[q->front];
    q->front = (q->front + 1) % q->capacity;
    return 1;
}
```

在演示中连续入队，直到队满；再出队几次，然后继续入队，观察 `rear` 怎样从 5 回到 0：

<div class="dsviz" data-viz="circular-queue"></div>

### 3.1 队空和队满

**队空**：`front == rear`，两个下标重合，中间没有元素。

**队满**：如果允许把数组全部填满，填满时 `rear` 绕了一圈，又和 `front` 重合了，`front == rear`，和队空**无法区分**。常用的解决办法有两种：

1. **留一个空位**（本文的做法）：当 `rear` 的下一个位置就是 `front` 时，就认为已经满了。长度为 n 的数组最多存放 n - 1 个元素。
2. **另外记录元素个数** `size`：队空是 `size == 0`，队满是 `size == capacity`，不浪费空间，但每次入队出队要多维护一个变量。

元素个数的计算 `(rear + capacity - front) % capacity`：`rear >= front` 时就是 `rear - front`；`rear` 回绕到了 `front` 前面时，要加上一圈的长度。先加 `capacity` 再取余，是为了避免无符号数相减得到负数（会回绕成一个巨大的数）。

### 3.2 测试

```c
Queue q;
queueInit(&q, 5);                           // 最多 4 个元素
for (int i = 1; i <= 5; ++i) printf("enqueue %d: %s\n", i * 10, enqueue(&q, i * 10) ? "成功" : "失败（队满）");
int x;
dequeue(&q, &x); printf("dequeue -> %d\n", x);
dequeue(&q, &x); printf("dequeue -> %d\n", x);
enqueue(&q, 50); enqueue(&q, 60);
printf("front = %zu, rear = %zu, size = %zu\n", q.front, q.rear, queueSize(&q));
printf("依次出队:");
while (dequeue(&q, &x)) printf(" %d", x);
printf("\n");
```

```
enqueue 10: 成功
enqueue 20: 成功
enqueue 30: 成功
enqueue 40: 成功
enqueue 50: 失败（队满）
dequeue -> 10
dequeue -> 20
front = 2, rear = 1, size = 4
依次出队: 30 40 50 60
```

出队两次后再入队，`rear` 回绕到了 1，比 `front` 还小，但元素个数和出队的顺序都是正确的。

需要自动扩容时，不能直接 `realloc`：回绕之后元素分成了数组末尾和开头两段，扩容后要把它们按顺序复制到新数组的开头。

## 4. 链式队列

用链表实现队列，需要两个指针：`front` 指向第一个节点（出队的一端），`rear` 指向最后一个节点（入队的一端），两种操作都是 O(1)：

```c
typedef struct QNode { int value; struct QNode* next; } QNode;
typedef struct { QNode* front; QNode* rear; size_t size; } LinkedQueue;

int lqEnqueue(LinkedQueue* q, int value)
{
    QNode* node = malloc(sizeof(QNode));
    if (node == NULL) return 0;
    node->value = value;
    node->next = NULL;
    if (q->rear == NULL) q->front = node;       // 原来是空队列
    else q->rear->next = node;
    q->rear = node;
    q->size++;
    return 1;
}

int lqDequeue(LinkedQueue* q, int* out)
{
    if (q->front == NULL) return 0;
    QNode* node = q->front;
    *out = node->value;
    q->front = node->next;
    if (q->front == NULL) q->rear = NULL;       // 队列变空，rear 也要清空
    free(node);
    q->size--;
    return 1;
}
```

为什么在链表头部出队、尾部入队，而不是反过来？因为单链表删除尾节点需要找到它的前驱，是 O(n) 的；而删除头节点和在尾部追加都是 O(1)。

**最容易出错的地方是队列变空的时候**：出队后如果 `front` 变成了 `NULL`，`rear` 必须同时设为 `NULL`。否则 `rear` 仍然指向刚刚释放的节点，下一次入队时 `q->rear->next = node` 就是释放后使用。

```
链式队列出队 1，剩余: 2 3 4，rear 是 NULL
```

全部出队后 `rear` 被正确地清空了。两种队列都用 `-fsanitize=address,undefined` 和 `leaks` 检查过。

## 5. 双端队列

**双端队列**（deque，double-ended queue）在**两端**都可以插入和删除。它同时具备栈和队列的能力：只用一端就是栈，一端进另一端出就是队列。

用循环数组实现双端队列，只需要再加上「在队头插入」（`front` 先减 1：`front = (front + capacity - 1) % capacity`）和「在队尾删除」两个操作。C++ 的 `std::deque` 和 Python 的 `collections.deque` 都是双端队列。下一篇的单调队列就会用到它。

## 6. 队列的应用

- **缓冲区**：生产者和消费者的速度不一致时，用队列暂存数据。键盘输入、网络数据包、《字符串与 IO 流》中的输出缓冲区都是这样。
- **任务调度**：操作系统用队列管理等待 CPU 的进程，按顺序轮流执行（循环链表一节中提到的时间片轮转）。
- **广度优先搜索**（BFS）：先访问离起点近的节点，再访问远的，需要用队列记录「待访问」的节点。第 3 章的层序遍历、第 4 章的图的遍历和最短路径都会用到。

## 7. 常见错误

1. **用数组实现队列，出队时移动所有元素**：O(n)。
2. **只让下标加 1，不取余**：假溢出，或者越界。
3. **队满的判断和队空一样，都是 `front == rear`**：无法区分。
4. **计算元素个数时直接 `rear - front`**：`rear` 回绕后得到负数（无符号数则是巨大的数）。
5. **循环队列直接 `realloc` 扩容**：回绕的元素顺序被打乱。
6. **链式队列出队后变空，没有把 `rear` 置为 `NULL`**：释放后使用。

## 8. 小结

- 队列在队尾入队、在队头出队，先进先出
- 数组实现要用循环队列：下标用 `(i + 1) % capacity` 前进，入队出队都是 O(1)；实测比移动元素的实现快了 1000 多倍
- 留一个空位区分队空（`front == rear`）和队满（`(rear + 1) % capacity == front`），或者另外记录元素个数
- 链式队列在头部出队、尾部入队；队列变空时 `rear` 也要置空
- 双端队列两端都可以操作；队列用于缓冲、调度和广度优先搜索

## 9. 练习

1. 只用两个栈实现一个队列，入队和出队的均摊时间复杂度都是 O(1)。
2. 改用「记录元素个数 `size`」的方法实现循环队列，写出 `isEmpty`、`isFull`、`enqueue`、`dequeue`。这样长度为 n 的数组可以存放几个元素？
3. 循环队列的数组长度是 8，`front = 6`，`rear = 2`，队列中有几个元素？它们分别在哪些下标上？

<details>
<summary>点击查看答案</summary>

1. 一个栈 `in` 负责入队，另一个栈 `out` 负责出队。出队时如果 `out` 是空的，就把 `in` 中的元素全部弹出并压入 `out`，这样顺序就反过来了，`out` 的栈顶就是最早入队的元素：

   ```c
   int tsEnqueue(TwoStackQueue* q, int v)
   {
       if (q->inTop == CAP) return 0;
       q->in[q->inTop++] = v;                      // 入队：压入 in 栈
       return 1;
   }

   int tsDequeue(TwoStackQueue* q, int* out)
   {
       if (q->outTop == 0) {                       // out 栈空了，才把 in 栈整个倒过去
           while (q->inTop > 0) {
               q->out[q->outTop++] = q->in[--q->inTop];
           }
       }
       if (q->outTop == 0) return 0;               // 两个栈都空：队列为空
       *out = q->out[--q->outTop];
       return 1;
   }
   ```

   入队 1 2 3，出队一个，再入队 4 5，然后全部出队，输出 `1 2 3 4 5`，顺序正确。每个元素最多被压入、弹出 `in` 一次，压入、弹出 `out` 一次，所以均摊 O(1)。注意只有 `out` 为空时才能倒，否则顺序会乱。

2. ```c
   int isEmpty(const Queue* q) { return q->size == 0; }
   int isFull(const Queue* q)  { return q->size == q->capacity; }

   int enqueue(Queue* q, int value)
   {
       if (isFull(q)) return 0;
       q->data[q->rear] = value;
       q->rear = (q->rear + 1) % q->capacity;
       q->size++;
       return 1;
   }

   int dequeue(Queue* q, int* out)
   {
       if (isEmpty(q)) return 0;
       *out = q->data[q->front];
       q->front = (q->front + 1) % q->capacity;
       q->size--;
       return 1;
   }
   ```

   可以存放 n 个元素，不需要留空位。

3. 元素个数 (2 + 8 - 6) % 8 = 4。它们在下标 6、7、0、1 上（`rear = 2` 是下一个要放的位置，不包括在内）。

</details>
