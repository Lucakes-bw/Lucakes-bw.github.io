---
id: 72
title: 划分字母区间
difficulty: medium
tags: [字符串, 贪心, 哈希表, 双指针]
leetcode: https://leetcode.cn/problems/partition-labels/
function: partitionLabels
params: { s: string }
returns: "int[]"
examples:
  - in: ["ababcbacadefegdehijhklij"]
    out: [9, 7, 8]
    explain: "划分为 \"ababcbaca\"、\"defegde\"、\"hijhklij\"，每个字母只出现在其中一段里。"
  - in: ["eccbbbbdec"]
    out: [10]
tests:
  - { in: ["a"], out: [1] }
  - { in: ["ab"], out: [1, 1] }
  - { in: ["aa"], out: [2] }
  - { in: ["abcdef"], out: [1, 1, 1, 1, 1, 1] }
  - { in: ["abca"], out: [4] }
  - { in: ["caedbdedda"], out: [1, 9] }
  - { in: ["qiejxqfnqceocmy"], out: [13, 1, 1] }
  - { in: ["abacbdxyzxyzxyz"], out: [5, 1, 9] }
  - { in: ["iqfkoyztwdzqsqfobvdfhxqdoamhnlhmaomzxicbdixgpmzllporraehsoykculqfgrblbktspsnkwdasopiqulgqqvplitjdpwurwfssxjvsxtkhzuzcjhukdkoddiydkalkuidurqqmqcaulvirzyhlruztkpoaklfcjvpxicxguzrhvmcngcwukhlfeehyflwmwrpovbsvidbqzmykjedzetdutgtcysycwdfdqjtbrsosvdjpzzfyglgckbqofhgodjyndyqkfurifwumymgzjykjscaxqgbqrlqekhbhthkyisrmaajltzazzlfyowadlepvdfqymnxautuqmuoaaklgnzxvqbmauhumitehuklgcdlufakglnntyjhlhbypfjkkteqcbljwbbnvpcptkyaqkfplxisjryerhjubirtzpcbdicbsflcpcjqvabprqxhcdoooovfzvvgelirweydyponkxehiyxvtppohnobxwfc"], out: [500] }
  - { in: ["aaaaaaaaaaaaaaaaaaaabbbbbbbbbbbbbbbbbbbbccccccccccccccccccccddddddddddddddddddddeeeeeeeeeeeeeeeeeeeeffffffffffffffffffffgggggggggggggggggggghhhhhhhhhhhhhhhhhhhhiiiiiiiiiiiiiiiiiiiijjjjjjjjjjjjjjjjjjjjkkkkkkkkkkkkkkkkkkkkllllllllllllllllllllmmmmmmmmmmmmmmmmmmmmnnnnnnnnnnnnnnnnnnnnooooooooooooooooooooppppppppppppppppppppqqqqqqqqqqqqqqqqqqqqrrrrrrrrrrrrrrrrrrrrssssssssssssssssssssttttttttttttttttttttuuuuuuuuuuuuuuuuuuuuvvvvvvvvvvvvvvvvvvvvwwwwwwwwwwwwwwwwwwwwxxxxxxxxxxxxxxxxxxxxyyyyyyyyyyyyyyyyyyyy"], out: [20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20] }
---
给定一个字符串 `s`，把它划分成**尽可能多**的片段，要求同一个字母只出现在其中一个片段里。

返回每个片段长度组成的数组，片段按在 `s` 中的顺序排列。注意：把所有片段按顺序拼起来应当还是 `s`。

**约束**

- `1 <= s.length <= 500`
- `s` 只包含小写英文字母

**提示**：先记录每个字母最后一次出现的位置。从左往右扫描，维护当前片段至少要延伸到的位置 `end = max(end, last[c])`；当扫描位置等于 `end` 时，一个片段就结束了。
