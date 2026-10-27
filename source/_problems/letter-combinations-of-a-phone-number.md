---
id: 54
title: 电话号码的字母组合
difficulty: medium
tags: [字符串, 回溯, 哈希表]
leetcode: https://leetcode.cn/problems/letter-combinations-of-a-phone-number/
function: letterCombinations
params: { digits: string }
returns: "string[]"
compare: unordered
examples:
  - in: ["23"]
    out: ["ad", "ae", "af", "bd", "be", "bf", "cd", "ce", "cf"]
    explain: "2 对应 abc，3 对应 def，两两组合共 9 种。"
  - in: [""]
    out: []
  - in: ["2"]
    out: ["a", "b", "c"]
tests:
  - { in: ["7"], out: ["p", "q", "r", "s"] }
  - { in: ["9"], out: ["w", "x", "y", "z"] }
  - { in: ["79"], out: ["pw", "px", "py", "pz", "qw", "qx", "qy", "qz", "rw", "rx", "ry", "rz", "sw", "sx", "sy", "sz"] }
  - { in: ["234"], out: ["adg", "adh", "adi", "aeg", "aeh", "aei", "afg", "afh", "afi", "bdg", "bdh", "bdi", "beg", "beh", "bei", "bfg", "bfh", "bfi", "cdg", "cdh", "cdi", "ceg", "ceh", "cei", "cfg", "cfh", "cfi"] }
  - { in: ["22"], out: ["aa", "ab", "ac", "ba", "bb", "bc", "ca", "cb", "cc"] }
  - { in: ["7979"], out: ["pwpw", "pwpx", "pwpy", "pwpz", "pwqw", "pwqx", "pwqy", "pwqz", "pwrw", "pwrx", "pwry", "pwrz", "pwsw", "pwsx", "pwsy", "pwsz", "pxpw", "pxpx", "pxpy", "pxpz", "pxqw", "pxqx", "pxqy", "pxqz", "pxrw", "pxrx", "pxry", "pxrz", "pxsw", "pxsx", "pxsy", "pxsz", "pypw", "pypx", "pypy", "pypz", "pyqw", "pyqx", "pyqy", "pyqz", "pyrw", "pyrx", "pyry", "pyrz", "pysw", "pysx", "pysy", "pysz", "pzpw", "pzpx", "pzpy", "pzpz", "pzqw", "pzqx", "pzqy", "pzqz", "pzrw", "pzrx", "pzry", "pzrz", "pzsw", "pzsx", "pzsy", "pzsz", "qwpw", "qwpx", "qwpy", "qwpz", "qwqw", "qwqx", "qwqy", "qwqz", "qwrw", "qwrx", "qwry", "qwrz", "qwsw", "qwsx", "qwsy", "qwsz", "qxpw", "qxpx", "qxpy", "qxpz", "qxqw", "qxqx", "qxqy", "qxqz", "qxrw", "qxrx", "qxry", "qxrz", "qxsw", "qxsx", "qxsy", "qxsz", "qypw", "qypx", "qypy", "qypz", "qyqw", "qyqx", "qyqy", "qyqz", "qyrw", "qyrx", "qyry", "qyrz", "qysw", "qysx", "qysy", "qysz", "qzpw", "qzpx", "qzpy", "qzpz", "qzqw", "qzqx", "qzqy", "qzqz", "qzrw", "qzrx", "qzry", "qzrz", "qzsw", "qzsx", "qzsy", "qzsz", "rwpw", "rwpx", "rwpy", "rwpz", "rwqw", "rwqx", "rwqy", "rwqz", "rwrw", "rwrx", "rwry", "rwrz", "rwsw", "rwsx", "rwsy", "rwsz", "rxpw", "rxpx", "rxpy", "rxpz", "rxqw", "rxqx", "rxqy", "rxqz", "rxrw", "rxrx", "rxry", "rxrz", "rxsw", "rxsx", "rxsy", "rxsz", "rypw", "rypx", "rypy", "rypz", "ryqw", "ryqx", "ryqy", "ryqz", "ryrw", "ryrx", "ryry", "ryrz", "rysw", "rysx", "rysy", "rysz", "rzpw", "rzpx", "rzpy", "rzpz", "rzqw", "rzqx", "rzqy", "rzqz", "rzrw", "rzrx", "rzry", "rzrz", "rzsw", "rzsx", "rzsy", "rzsz", "swpw", "swpx", "swpy", "swpz", "swqw", "swqx", "swqy", "swqz", "swrw", "swrx", "swry", "swrz", "swsw", "swsx", "swsy", "swsz", "sxpw", "sxpx", "sxpy", "sxpz", "sxqw", "sxqx", "sxqy", "sxqz", "sxrw", "sxrx", "sxry", "sxrz", "sxsw", "sxsx", "sxsy", "sxsz", "sypw", "sypx", "sypy", "sypz", "syqw", "syqx", "syqy", "syqz", "syrw", "syrx", "syry", "syrz", "sysw", "sysx", "sysy", "sysz", "szpw", "szpx", "szpy", "szpz", "szqw", "szqx", "szqy", "szqz", "szrw", "szrx", "szry", "szrz", "szsw", "szsx", "szsy", "szsz"] }
---
手机九宫格键盘上，数字 `2` 到 `9` 分别对应几个字母：`2` abc、`3` def、`4` ghi、`5` jkl、`6` mno、`7` pqrs、`8` tuv、`9` wxyz。

给定一个只包含数字 `2-9` 的字符串 `digits`，返回它能表示的所有字母组合，顺序不限。如果 `digits` 为空，返回空数组。

**约束**

- `0 <= digits.length <= 4`
- `digits[i]` 是 `'2'` 到 `'9'` 之间的数字

**提示**：每一位数字从对应的字母里选一个，用回溯一位一位地拼出来。
