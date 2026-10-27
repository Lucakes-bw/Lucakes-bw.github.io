---
id: 87
title: 编辑距离
difficulty: medium
tags: [字符串, 动态规划]
leetcode: https://leetcode.cn/problems/edit-distance/
function: minDistance
params: { word1: string, word2: string }
returns: int
examples:
  - in: ["horse", "ros"]
    out: 3
    explain: "horse → rorse（把 h 换成 r）→ rose（删除 r）→ ros（删除 e），共 3 步。"
  - in: ["intention", "execution"]
    out: 5
tests:
  - { in: ["", ""], out: 0 }
  - { in: ["", "abc"], out: 3 }
  - { in: ["abc", ""], out: 3 }
  - { in: ["a", "a"], out: 0 }
  - { in: ["a", "b"], out: 1 }
  - { in: ["ab", "ba"], out: 2 }
  - { in: ["kitten", "sitting"], out: 3 }
  - { in: ["sunday", "saturday"], out: 3 }
  - { in: ["dinitrophenylhydrazine", "acetylphenylhydrazine"], out: 6 }
  - { in: ["aabcccaaabbcabaaacbcabcbbcbabbbccbcbcbbcabbcccbaaacaaababbaacbcbcbbaacbacbcbbbabcacbbabaaabbbcaaabaaababcaccabcaacbaabbcbaccbabbbbbacaaccbaacaccbcacabacccbbbabbccaabaaaacabababcacacbabccbcbbacbacbbbbbccbbcbbaacccabacbaababbcacacabbcabbcbbcbabaabacacbaacbcabcababaaaacaacccaaaacbcbbabaaaccabaacbbbacbb", "acabbbbcaabbccbcaaabbccbabbaabcabaacbbbbcaaacabbcaabccacbcccbbbcacbaaaabbbbcacacbababcbaaaabccccbbcbaccacacccbcbabcaabcbbaacacbaacaabbaabcccbaabbbbcbaabbbacbaccaabcbbaabcccaabcabacabbccccbccaabbcbccbbbbcacbabcccabaabcccaabacbcacbbacbbcaacaacbcccbbcab"], out: 128 }
  - { in: ["aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"], out: 300 }
---
给定两个单词 `word1` 和 `word2`，返回把 `word1` 转换成 `word2` 所用的**最少操作数**。可以对一个单词进行三种操作：

- 插入一个字符
- 删除一个字符
- 替换一个字符

**约束**

- `0 <= word1.length, word2.length <= 500`
- 只包含小写英文字母

**提示**：`f[i][j]` 表示 `word1` 前 `i` 个字符变成 `word2` 前 `j` 个字符的最少操作数。末尾字符相同时等于 `f[i-1][j-1]`；否则是 `1 + min(f[i-1][j]（删除）, f[i][j-1]（插入）, f[i-1][j-1]（替换）)`。
