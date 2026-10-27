---
id: 78
title: 单词拆分
difficulty: medium
tags: [字符串, 动态规划, 哈希表, 字典树]
leetcode: https://leetcode.cn/problems/word-break/
function: wordBreak
params: { s: string, wordDict: "string[]" }
returns: bool
examples:
  - in: ["leetcode", ["leet", "code"]]
    out: true
    explain: "\"leetcode\" 可以拆成 \"leet code\"。"
  - in: ["applepenapple", ["apple", "pen"]]
    out: true
    explain: "字典里的单词可以重复使用。"
  - in: ["catsandog", ["cats", "dog", "sand", "and", "cat"]]
    out: false
tests:
  - { in: ["a", ["a"]], out: true }
  - { in: ["a", ["b"]], out: false }
  - { in: ["aaaaaaa", ["aaaa", "aaa"]], out: true }
  - { in: ["cars", ["car", "ca", "rs"]], out: true }
  - { in: ["goalspecial", ["go", "goal", "goals", "special"]], out: true }
  - { in: ["bb", ["a", "b", "bbb", "bbbb"]], out: true }
  - { in: ["aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab", ["a", "aa", "aaa", "aaaa", "aaaaa", "aaaaaa", "aaaaaaa", "aaaaaaaa", "aaaaaaaaa", "aaaaaaaaaa"]], out: false }
  - { in: ["aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", ["a", "aa", "aaa", "aaaa", "aaaaa", "aaaaaa", "aaaaaaa", "aaaaaaaa", "aaaaaaaaa", "aaaaaaaaaa"]], out: true }
  - { in: ["catsanddogcatsanddog", ["cat", "cats", "and", "sand", "dog"]], out: true }
---
给定一个字符串 `s` 和一个字符串列表 `wordDict` 作为字典，判断能否把 `s` 拆分成若干个字典中的单词（用空格分开）。

字典中的单词可以重复使用，也可以不全用上。

**约束**

- `1 <= s.length <= 300`
- `1 <= wordDict.length <= 1000`
- `1 <= wordDict[i].length <= 20`
- `s` 和 `wordDict[i]` 只包含小写英文字母，`wordDict` 中的字符串互不相同

**提示**：`f[i]` 表示 `s` 的前 `i` 个字符能否拆分。`f[i]` 为真，当且仅当存在 `j < i`，使得 `f[j]` 为真并且 `s[j..i)` 在字典里。直接暴力回溯在 `"aaaa…ab"` 这样的输入上会指数级超时。
