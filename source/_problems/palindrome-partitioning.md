---
id: 58
title: 分割回文串
difficulty: medium
tags: [字符串, 回溯, 动态规划]
leetcode: https://leetcode.cn/problems/palindrome-partitioning/
function: partition
params: { s: string }
returns: "string[][]"
compare: unordered
examples:
  - in: ["aab"]
    out: [["aa", "b"], ["a", "a", "b"]]
  - in: ["a"]
    out: [["a"]]
tests:
  - { in: ["ab"], out: [["a", "b"]] }
  - { in: ["aa"], out: [["aa"], ["a", "a"]] }
  - { in: ["aba"], out: [["aba"], ["a", "b", "a"]] }
  - { in: ["abba"], out: [["abba"], ["a", "bb", "a"], ["a", "b", "b", "a"]] }
  - { in: ["efe"], out: [["efe"], ["e", "f", "e"]] }
  - { in: ["racecar"], out: [["racecar"], ["r", "aceca", "r"], ["r", "a", "cec", "a", "r"], ["r", "a", "c", "e", "c", "a", "r"]] }
  - { in: ["abcde"], out: [["a", "b", "c", "d", "e"]] }
  - { in: ["aaaaaaaa"], out: [["aaaaaaaa"], ["a", "aaaaaaa"], ["aa", "aaaaaa"], ["a", "a", "aaaaaa"], ["aaa", "aaaaa"], ["a", "aa", "aaaaa"], ["aa", "a", "aaaaa"], ["a", "a", "a", "aaaaa"], ["aaaa", "aaaa"], ["a", "aaa", "aaaa"], ["aa", "aa", "aaaa"], ["a", "a", "aa", "aaaa"], ["aaa", "a", "aaaa"], ["a", "aa", "a", "aaaa"], ["aa", "a", "a", "aaaa"], ["a", "a", "a", "a", "aaaa"], ["aaaaa", "aaa"], ["a", "aaaa", "aaa"], ["aa", "aaa", "aaa"], ["a", "a", "aaa", "aaa"], ["aaa", "aa", "aaa"], ["a", "aa", "aa", "aaa"], ["aa", "a", "aa", "aaa"], ["a", "a", "a", "aa", "aaa"], ["aaaa", "a", "aaa"], ["a", "aaa", "a", "aaa"], ["aa", "aa", "a", "aaa"], ["a", "a", "aa", "a", "aaa"], ["aaa", "a", "a", "aaa"], ["a", "aa", "a", "a", "aaa"], ["aa", "a", "a", "a", "aaa"], ["a", "a", "a", "a", "a", "aaa"], ["aaaaaa", "aa"], ["a", "aaaaa", "aa"], ["aa", "aaaa", "aa"], ["a", "a", "aaaa", "aa"], ["aaa", "aaa", "aa"], ["a", "aa", "aaa", "aa"], ["aa", "a", "aaa", "aa"], ["a", "a", "a", "aaa", "aa"], ["aaaa", "aa", "aa"], ["a", "aaa", "aa", "aa"], ["aa", "aa", "aa", "aa"], ["a", "a", "aa", "aa", "aa"], ["aaa", "a", "aa", "aa"], ["a", "aa", "a", "aa", "aa"], ["aa", "a", "a", "aa", "aa"], ["a", "a", "a", "a", "aa", "aa"], ["aaaaa", "a", "aa"], ["a", "aaaa", "a", "aa"], ["aa", "aaa", "a", "aa"], ["a", "a", "aaa", "a", "aa"], ["aaa", "aa", "a", "aa"], ["a", "aa", "aa", "a", "aa"], ["aa", "a", "aa", "a", "aa"], ["a", "a", "a", "aa", "a", "aa"], ["aaaa", "a", "a", "aa"], ["a", "aaa", "a", "a", "aa"], ["aa", "aa", "a", "a", "aa"], ["a", "a", "aa", "a", "a", "aa"], ["aaa", "a", "a", "a", "aa"], ["a", "aa", "a", "a", "a", "aa"], ["aa", "a", "a", "a", "a", "aa"], ["a", "a", "a", "a", "a", "a", "aa"], ["aaaaaaa", "a"], ["a", "aaaaaa", "a"], ["aa", "aaaaa", "a"], ["a", "a", "aaaaa", "a"], ["aaa", "aaaa", "a"], ["a", "aa", "aaaa", "a"], ["aa", "a", "aaaa", "a"], ["a", "a", "a", "aaaa", "a"], ["aaaa", "aaa", "a"], ["a", "aaa", "aaa", "a"], ["aa", "aa", "aaa", "a"], ["a", "a", "aa", "aaa", "a"], ["aaa", "a", "aaa", "a"], ["a", "aa", "a", "aaa", "a"], ["aa", "a", "a", "aaa", "a"], ["a", "a", "a", "a", "aaa", "a"], ["aaaaa", "aa", "a"], ["a", "aaaa", "aa", "a"], ["aa", "aaa", "aa", "a"], ["a", "a", "aaa", "aa", "a"], ["aaa", "aa", "aa", "a"], ["a", "aa", "aa", "aa", "a"], ["aa", "a", "aa", "aa", "a"], ["a", "a", "a", "aa", "aa", "a"], ["aaaa", "a", "aa", "a"], ["a", "aaa", "a", "aa", "a"], ["aa", "aa", "a", "aa", "a"], ["a", "a", "aa", "a", "aa", "a"], ["aaa", "a", "a", "aa", "a"], ["a", "aa", "a", "a", "aa", "a"], ["aa", "a", "a", "a", "aa", "a"], ["a", "a", "a", "a", "a", "aa", "a"], ["aaaaaa", "a", "a"], ["a", "aaaaa", "a", "a"], ["aa", "aaaa", "a", "a"], ["a", "a", "aaaa", "a", "a"], ["aaa", "aaa", "a", "a"], ["a", "aa", "aaa", "a", "a"], ["aa", "a", "aaa", "a", "a"], ["a", "a", "a", "aaa", "a", "a"], ["aaaa", "aa", "a", "a"], ["a", "aaa", "aa", "a", "a"], ["aa", "aa", "aa", "a", "a"], ["a", "a", "aa", "aa", "a", "a"], ["aaa", "a", "aa", "a", "a"], ["a", "aa", "a", "aa", "a", "a"], ["aa", "a", "a", "aa", "a", "a"], ["a", "a", "a", "a", "aa", "a", "a"], ["aaaaa", "a", "a", "a"], ["a", "aaaa", "a", "a", "a"], ["aa", "aaa", "a", "a", "a"], ["a", "a", "aaa", "a", "a", "a"], ["aaa", "aa", "a", "a", "a"], ["a", "aa", "aa", "a", "a", "a"], ["aa", "a", "aa", "a", "a", "a"], ["a", "a", "a", "aa", "a", "a", "a"], ["aaaa", "a", "a", "a", "a"], ["a", "aaa", "a", "a", "a", "a"], ["aa", "aa", "a", "a", "a", "a"], ["a", "a", "aa", "a", "a", "a", "a"], ["aaa", "a", "a", "a", "a", "a"], ["a", "aa", "a", "a", "a", "a", "a"], ["aa", "a", "a", "a", "a", "a", "a"], ["a", "a", "a", "a", "a", "a", "a", "a"]] }
  - { in: ["abacabadab"], out: [["a", "b", "aca", "badab"], ["aba", "c", "a", "badab"], ["a", "b", "a", "c", "a", "badab"], ["a", "bacab", "ada", "b"], ["a", "b", "aca", "b", "ada", "b"], ["aba", "c", "a", "b", "ada", "b"], ["a", "b", "a", "c", "a", "b", "ada", "b"], ["abacaba", "d", "a", "b"], ["aba", "c", "aba", "d", "a", "b"], ["a", "b", "a", "c", "aba", "d", "a", "b"], ["a", "bacab", "a", "d", "a", "b"], ["a", "b", "aca", "b", "a", "d", "a", "b"], ["aba", "c", "a", "b", "a", "d", "a", "b"], ["a", "b", "a", "c", "a", "b", "a", "d", "a", "b"]] }
---
给定字符串 `s`，把它分割成若干个子串，使每个子串都是**回文串**。返回所有可能的分割方案，方案之间的顺序不限（每个方案内部按原字符串中的顺序排列）。

**约束**

- `1 <= s.length <= 16`
- `s` 只包含小写英文字母

**提示**：回溯枚举第一刀切在哪里，切下来的前缀是回文才继续递归。可以先用动态规划预处理 `isPal[i][j]`，表示 `s[i..j]` 是否回文。
