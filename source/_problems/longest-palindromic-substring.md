---
id: 85
title: 最长回文子串
difficulty: medium
tags: [字符串, 动态规划, 双指针]
leetcode: https://leetcode.cn/problems/longest-palindromic-substring/
function: longestPalindrome
params: { s: string }
returns: string
examples:
  - in: ["xabay"]
    out: "aba"
    explain: "\"aba\" 是最长的回文子串。"
  - in: ["cbbd"]
    out: "bb"
    explain: "\"bb\" 是最长的回文子串。"
tests:
  - { in: ["a"], out: "a" }
  - { in: ["aa"], out: "aa" }
  - { in: ["abcba"], out: "abcba" }
  - { in: ["abacdfgdcaba"], out: "aba" }
  - { in: ["forgeeksskeegfor"], out: "geeksskeeg" }
  - { in: ["racecarxyz"], out: "racecar" }
  - { in: ["xyzracecar"], out: "racecar" }
  - { in: ["aaaa"], out: "aaaa" }
  - { in: ["0123210abc"], out: "0123210" }
  - { in: ["ggbgeddbecffehgcdadeadbhdecbbhddfchehebgabcbagghchfbebfaffbdeacahecgdaeeagcdahcchhhbghedbdhedebcbffaheffbdhaegdhebcgbfehfbchgbhebagdcgehfaadfehebaffghbfedghdgfffchahdhfbbahcecfdcgefdacdcgffcadgbchbfhbgeacdafbadgdfhfafdagahfffehfaecchdghgghcbbfcabcacegffbfdbhbdbbhfbagfaahhedhabgabgcabfhhafagcgebheaaacbhheebcfbedfffaebgdeeachbghdabghhbddhghahafecdfbeffdddffbehefbcgddggfheafhfdcbbcbddgceahaeeaefdhdbdgezegdbdhdfeaeeahaecgddbcbbcdfhfaehfggddgcbbcbaeehdagcahbcdcghhdbbhgfdafgbdehgchdbdhhdadcccggdbfabchfbeegfahbhgdcdcbehaafheeacchfhfacfdhheeebfgdadebdccabbafabbhedeefgefbecfchfbhcbhhdhhgbhbabfacdchhbggdgcbfdeefdabcbhacfbfbdafgfhgccbaddfaagcfeaefgfhhcagccfbhagfhddagebhdffbabhafghdbaaabdebdbfdbbfgdcfdbaeadabdhgedfaeedfdhfecfbbaagceecfebgceehcebhebfbgafbdeagbcbghehefddbbahdgebffeecbeeaafgbdhcbfgghfbabaadfbdbcbefghbgbehdghfahfcbfcchedebadhggeahbaechgabeeggagbhcagchehahhhcgfbggcfegh"], out: "bcgddggfheafhfdcbbcbddgceahaeeaefdhdbdgezegdbdhdfeaeeahaecgddbcbbcdfhfaehfggddgcb" }
  - { in: ["aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"], out: "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" }
---
给定字符串 `s`，找出其中**最长的回文子串**。

回文串是正着读和反着读都一样的字符串。本题的所有用例都保证**最长的回文子串只有一个**。

**约束**

- `1 <= s.length <= 1000`
- `s` 由数字和英文字母组成

**提示**：中心扩展：回文串以某个字符（奇数长度）或某两个字符之间（偶数长度）为中心对称，一共 `2n - 1` 个中心，从每个中心向两边扩展，O(n²)。也可以用动态规划：`s[i..j]` 是回文当且仅当 `s[i] == s[j]` 且 `s[i+1..j-1]` 是回文。
