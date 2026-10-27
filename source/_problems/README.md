# 题库

每道题一个 `<slug>.md`，生成 `/problems/<slug>/`。这个目录以 `_` 开头，Hexo 不会把它当页面发布；
`solutions/` 下的参考解答也不会发布，只给检查脚本用。

## front-matter

```yaml
id: 1                        # 题号，列表按它排序
title: 两数之和
difficulty: easy             # easy / medium / hard
tags: [数组, 哈希表]
related: [ds-hash-table]     # 相关文章，写 source/_posts 下的文件名（不带 .md）
leetcode: https://leetcode.cn/problems/two-sum/    # 可选，原题链接
time_limit: 3000             # 可选，一次运行的时间限制（毫秒），Python 自动 ×3

function: twoSum             # 函数名（C++ / Python 是 Solution 类的方法）
params: { nums: "int[]", target: int }   # 参数名: 类型，按顺序
returns: "int[]"
compare: unordered           # 可选：exact（默认）/ unordered / unordered-deep / float

examples:                    # 示例：显示在题面里，「运行」默认用它们
  - in: [[2, 7, 11, 15], 9]  # 每个参数一项
    out: [0, 1]
    explain: 可选的解释，支持 Markdown
tests:                       # 「提交」时在示例之后再跑这些
  - { in: [[3, 2, 4], 6], out: [1, 2] }
```

- 类型：`int` `long` `double` `bool` `string` `char` `ListNode` `TreeNode` `Node`，后面可以接 `[]`，比如 `int[][]`、`char[][]`
- 链表写成数组；二叉树按层序写成数组，空节点写 `null`
- 原地修改的题：`returns: void`，再用 `output: nums` 指定判题时检查哪个参数
- 题面用自己的话写，不要照搬 LeetCode 的原文

### 设计类的题目

不写 `function` / `params` / `returns`，改写 `design`。用例和 LeetCode 一样是「操作名数组、参数数组」，
输出是每次调用的返回值，构造函数和 `void` 方法的位置是 `null`：

```yaml
design:
  class: LRUCache
  init: { capacity: int }                  # 构造函数的参数，没有就写 {}
  methods:
    get: { params: { key: int }, returns: int }
    put: { params: { key: int, value: int } }   # 不写 returns 就是 void
examples:
  - in: [["LRUCache", "put", "get"], [[2], [1, 1], [1]]]
    out: [null, null, 1]
```

### 特殊输入

有些题的参数是「原结构里的某个节点」，用例里的数据和函数参数对不上。写 `input: <类型>`，
`params` 照常写函数的参数，用例按下表的格式写，判题程序搭好结构再调用：

| input | 用例 | 传给函数 | 返回节点时判题输出 |
| --- | --- | --- | --- |
| `cycle` | `[head 的值, pos]`，尾节点接到下标 `pos`，`-1` 无环 | `head` | 节点在链表中的下标，`null` 为 `-1` |
| `intersect` | `[listA, listB, skipA, skipB]`，B 的前 `skipB` 个节点之后接到 A 的第 `skipA` 个节点 | `headA, headB` | 节点在 A 中的下标，`null` 为 `-1` |
| `random` | `[[val, random 的下标或 null], ...]` | `head`（`Node`，带 `random`） | 同样格式，并检查没有用到原链表的节点 |
| `lca` | `[层序数组, p 的值, q 的值]`，树中的值互不相同 | `root, p, q`（都是节点） | 节点的值，并检查它在原树中 |

判题比较的是节点本身，返回值相等但不是同一个节点会判错。

## 检查

新增或修改题目后跑一遍，确认格式正确、C++ 模板能编译、三种语言的参考解答都能通过全部用例：

```bash
npm i --no-save @yowasp/clang@22.0.0-git20542-10   # 只需要装一次，C++ 编译器和网页里用的是同一个
node tools/check-problems.mjs                      # 也可以只检查某几题：node tools/check-problems.mjs two-sum
```

参考解答放在 `solutions/<slug>.cpp|py|js`，写法和网页里提交的代码一样。
