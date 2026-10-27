---
id: 57
title: 单词搜索
difficulty: medium
tags: [数组, 矩阵, 回溯, DFS]
leetcode: https://leetcode.cn/problems/word-search/
function: exist
params: { board: "char[][]", word: string }
returns: bool
examples:
  - in: [[["A", "B", "C", "E"], ["S", "F", "C", "S"], ["A", "D", "E", "E"]], "ABCCED"]
    out: true
    explain: "从左上角的 A 出发：A → B → C → C → E → D。"
  - in: [[["A", "B", "C", "E"], ["S", "F", "C", "S"], ["A", "D", "E", "E"]], "SEE"]
    out: true
  - in: [[["A", "B", "C", "E"], ["S", "F", "C", "S"], ["A", "D", "E", "E"]], "ABCB"]
    out: false
    explain: "第二个 B 只能再用一次左上角的 B，同一个格子不能重复使用。"
tests:
  - { in: [[["a"]], "a"], out: true }
  - { in: [[["a"]], "b"], out: false }
  - { in: [[["a", "b"]], "ba"], out: true }
  - { in: [[["a", "b"], ["c", "d"]], "abdc"], out: true }
  - { in: [[["a", "b"], ["c", "d"]], "abcd"], out: false }
  - { in: [[["a", "a"]], "aaa"], out: false }
  - { in: [[["A", "B", "C", "E"], ["S", "F", "C", "S"], ["A", "D", "E", "E"]], "ESEECCBASADF"], out: true }
  - { in: [[["A", "B", "C", "E"], ["S", "F", "C", "S"], ["A", "D", "E", "E"]], "ESEECCBASADFE"], out: false }
  - { in: [[["C", "A", "A"], ["A", "A", "A"], ["B", "C", "D"]], "AAB"], out: true }
  - { in: [[["A", "A", "A", "A", "A", "A"], ["A", "A", "A", "A", "A", "A"], ["A", "A", "A", "A", "A", "A"], ["A", "A", "A", "A", "A", "A"], ["A", "A", "A", "A", "A", "A"], ["A", "A", "A", "A", "A", "A"]], "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"], out: true }
  - { in: [[["A", "A", "A", "A", "A", "A"], ["A", "A", "A", "A", "A", "A"], ["A", "A", "A", "A", "A", "A"], ["A", "A", "A", "A", "A", "A"], ["A", "A", "A", "A", "A", "A"], ["A", "A", "A", "A", "A", "A"]], "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAB"], out: false }
  - { in: [[["a", "B"], ["b", "A"]], "aBAb"], out: true }
---
给定一个 `m x n` 的字符网格 `board` 和一个单词 `word`，判断 `word` 是否存在于网格中。

单词必须由**相邻**格子中的字母按顺序连成，相邻指上下左右四个方向。同一个格子里的字母在一个单词中不能重复使用。

**约束**

- `1 <= m, n <= 6`
- `1 <= word.length <= 36`
- `board` 和 `word` 只由大小写英文字母组成（区分大小写）

**进阶**：你能用剪枝让搜索在更大的网格上也够快吗？

**提示**：从每个格子出发做 DFS，走过的格子临时改成一个特殊字符，回溯时再改回来。可以先统计字母个数：网格里某个字母比单词里少，直接返回 `false`。
