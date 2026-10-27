---
id: 20
title: 最小覆盖子串
difficulty: hard
tags: [字符串, 哈希表, 滑动窗口]
leetcode: https://leetcode.cn/problems/minimum-window-substring/
function: minWindow
params: { s: string, t: string }
returns: string
examples:
  - in: ["ADOBECODEBANC", "ABC"]
    out: "BANC"
    explain: "包含 A、B、C 的最短子串是 \"BANC\"。"
  - in: ["a", "a"]
    out: "a"
  - in: ["a", "aa"]
    out: ""
    explain: "`t` 里有两个 a，而 `s` 只有一个，不存在符合要求的子串，返回空字符串。"
tests:
  - { in: ["ab", "b"], out: "b" }
  - { in: ["bba", "ab"], out: "ba" }
  - { in: ["abc", "cba"], out: "abc" }
  - { in: ["aaflslflsldkalskaaa", "aaa"], out: "aaa" }
  - { in: ["cabwefgewcwaefgcf", "cae"], out: "cwae" }
  - { in: ["xyz", "w"], out: "" }
  - { in: ["aAbBcC", "Ab"], out: "Ab" }
  - { in: ["acbbaca", "aba"], out: "baca" }
  - { in: ["abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzQ", "zQ"], out: "zQ" }
---
给定两个字符串 `s` 和 `t`，在 `s` 中找出**最短的子串**，使得 `t` 中的每个字符（包括重复的）都出现在这个子串里。

- 如果 `t` 中某个字符出现了 `m` 次，子串里这个字符也要至少出现 `m` 次
- 如果不存在这样的子串，返回空字符串 `""`
- 如果存在，保证答案唯一

**约束**

- `1 <= s.length, t.length <= 10^5`
- `s` 和 `t` 由大小写英文字母组成（区分大小写）

**进阶**：你能在 O(m + n) 时间内解决吗？

**提示**：右端不断扩张窗口，直到窗口覆盖了 `t`；然后收缩左端，在仍然覆盖的前提下尽量缩短，并记录答案。用一个计数变量记录「还差几个字符没覆盖」，就不用每次都比较整个计数表。
