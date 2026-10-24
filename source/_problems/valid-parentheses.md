---
id: 2
title: 有效的括号
difficulty: easy
tags: [栈, 字符串]
related: [stack]
leetcode: https://leetcode.cn/problems/valid-parentheses/
function: isValid
params: { s: string }
returns: bool
examples:
  - in: ["()[]{}"]
    out: true
  - in: ["(]"]
    out: false
    explain: "`(` 只能和 `)` 配对。"
  - in: ["{[()]}"]
    out: true
tests:
  - { in: ["("], out: false }
  - { in: [")"], out: false }
  - { in: ["([)]"], out: false }
  - { in: ["(([]){})"], out: true }
  - { in: ["]]]"], out: false }
  - { in: ["(((((((((())))))))))"], out: true }
  - { in: ["(){}}{"], out: false }
  - { in: ["[({})](){}[[]]"], out: true }
---
字符串 `s` 只包含 `(`、`)`、`[`、`]`、`{`、`}` 六种字符，判断它是不是一个**合法的括号序列**：

1. 每个左括号都必须用**同类型**的右括号闭合；
2. 左括号必须按**正确的顺序**闭合，也就是后打开的先关闭；
3. 每个右括号前面都要有一个还没闭合的同类型左括号。

**约束**

- `1 <= s.length <= 10^4`
- `s` 只由 `()[]{}` 组成

**提示**：遇到左括号先记下来，遇到右括号时要和「最近一个还没闭合的左括号」比较，这正好是栈的后进先出。
