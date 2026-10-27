---
id: 66
title: 字符串解码
difficulty: medium
tags: [字符串, 栈, 递归]
related: [stack]
leetcode: https://leetcode.cn/problems/decode-string/
function: decodeString
params: { s: string }
returns: string
examples:
  - in: ["3[a]2[bc]"]
    out: "aaabcbc"
  - in: ["3[a2[c]]"]
    out: "accaccacc"
    explain: "先展开里层：a2[c] = acc，再重复 3 次。"
  - in: ["2[abc]3[cd]ef"]
    out: "abcabccdcdcdef"
tests:
  - { in: ["a"], out: "a" }
  - { in: ["abc"], out: "abc" }
  - { in: ["1[z]"], out: "z" }
  - { in: ["10[a]"], out: "aaaaaaaaaa" }
  - { in: ["ab3[x]y"], out: "abxxxy" }
  - { in: ["2[2[2[b]]]"], out: "bbbbbbbb" }
  - { in: ["3[z]2[2[y]pq4[2[jk]e1[f]]]ef"], out: "zzzyypqjkjkefjkjkefjkjkefjkjkefyypqjkjkefjkjkefjkjkefjkjkefef" }
  - { in: ["100[leet]"], out: "leetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleetleet" }
  - { in: ["2[a10[bc]]d"], out: "abcbcbcbcbcbcbcbcbcbcabcbcbcbcbcbcbcbcbcbcd" }
  - { in: ["0[abc]x"], out: "x" }
---
给定一个经过编码的字符串，返回它解码后的结果。

编码规则是 `k[encoded_string]`：方括号里的 `encoded_string` 重复 `k` 次。`k` 是非负整数，方括号可以嵌套。

可以假设输入总是有效的：没有多余的空格，方括号总是成对出现；原始数据里不含数字，所有数字都只表示重复次数，比如不会出现 `3a` 或 `2[4]` 这样的输入。

**约束**

- `1 <= s.length <= 30`
- `s` 由小写英文字母、数字和方括号组成
- `0 <= k <= 300`
- 解码后的长度不超过 `10^5`

**提示**：用栈：遇到 `[` 时把当前已经拼好的字符串和重复次数压栈，重新开始拼；遇到 `]` 时弹出，把刚拼好的部分重复相应次数，接到弹出的字符串后面。数字可能不止一位。
