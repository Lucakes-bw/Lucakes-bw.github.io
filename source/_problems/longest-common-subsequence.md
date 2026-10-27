---
id: 86
title: 最长公共子序列
difficulty: medium
tags: [字符串, 动态规划]
leetcode: https://leetcode.cn/problems/longest-common-subsequence/
function: longestCommonSubsequence
params: { text1: string, text2: string }
returns: int
examples:
  - in: ["abcde", "ace"]
    out: 3
    explain: "最长公共子序列是 \"ace\"，长度 3。"
  - in: ["abc", "abc"]
    out: 3
  - in: ["abc", "def"]
    out: 0
tests:
  - { in: ["a", "a"], out: 1 }
  - { in: ["a", "b"], out: 0 }
  - { in: ["ab", "ba"], out: 1 }
  - { in: ["bsbininm", "jmjkbkjkv"], out: 1 }
  - { in: ["oxcpqrsvwf", "shmtulqrypy"], out: 2 }
  - { in: ["ezupkr", "ubmrapg"], out: 2 }
  - { in: ["aadcbaccddbbdcdbadabcccbadbbbcacbbacbcdaaaaccdbbcddadacacdabddbcbbbacaabbaccdbcdbdcabccacbdbbcbbcddbdbaadacbacbacbacbababadcddddaccdcadaddcbcadcbccbdcdabdcbabbdbabaccbacbcdddaabccbbacdbdcbdacdcdaacbabddbcdcdbacccbdcbdcaddcdadcadabdbcddcbcdbbadabdcccddbdddaabcacacdbddbdcacabaadcbbaddabdbdaadcddacbada", "dccacccccbcccaacdddbddabcadabbcdabbabdbbddcaadccacbabcadbdcddacaddcadbbaaadaadbcaabadcbdabbdcbcbccdcbadabdbadcccdbadccbcbddaacaddccdcdbddddacddccadddbaaabdcdcbabddcbbddbccddbdbdcdccbbdaadbaadadbbbdbcdbcaaaaacdbcdcabccccccbdcbcccacdabcaabdbddcddccaacbabdcbdcbaadcaccdacccadbdcabdcbaacdcdaaacdcddbccdcb"], out: 193 }
  - { in: ["aaaabbabaaabaaaaabbbabbbaabaaaaabbaababaaabababbbaaabbabaababaabbabaaabbaababbbababaaaababbbaabbbabaababaaaaaaaaabaaaabbabbabaababbbaaabaaaaaabababaabbabababbaaabbaaabababbaaaaababaaabbbaabbabbabbbbbaabaabbabaabbababbaabbbbabaaaaaabbbbaaababbbbbaaaaabbbabbbabbabaabbabbaaabbbbaabababaaaababbaaaabaabb", "cdcccccccdddcdddcddccccdddddddcdccdcddcdcccdcdcddcdccddcdcdcddcccdddddddddddccccccddddccddcccddddcccddccdcddddcdccdccdcdcdccdccccccdddccccddddddcdcddcdddcccdddcccddccdccdcddcddddcdddddcdccddcddccccdcd"], out: 0 }
  - { in: ["aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"], out: 150 }
---
给定两个字符串 `text1` 和 `text2`，返回它们的**最长公共子序列**的长度；如果没有公共子序列，返回 `0`。

子序列是从字符串中删去某些字符（也可以不删）、不改变其余字符相对顺序得到的新字符串，比如 `"ace"` 是 `"abcde"` 的子序列。公共子序列是两个字符串都含有的子序列。

**约束**

- `1 <= text1.length, text2.length <= 1000`
- 只包含小写英文字母

**提示**：`f[i][j]` 表示 `text1` 前 `i` 个字符和 `text2` 前 `j` 个字符的 LCS 长度。末尾字符相同时 `f[i][j] = f[i-1][j-1] + 1`，否则 `f[i][j] = max(f[i-1][j], f[i][j-1])`。
