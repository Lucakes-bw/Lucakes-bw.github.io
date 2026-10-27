---
id: 11
title: 字母异位词分组
difficulty: medium
tags: [数组, 哈希表, 字符串, 排序]
related: [ds-hash-table]
leetcode: https://leetcode.cn/problems/group-anagrams/
function: groupAnagrams
params: { strs: "string[]" }
returns: "string[][]"
compare: unordered-deep
examples:
  - in: [["eat", "tea", "tan", "ate", "nat", "bat"]]
    out: [["eat", "tea", "ate"], ["tan", "nat"], ["bat"]]
  - in: [[""]]
    out: [[""]]
  - in: [["a"]]
    out: [["a"]]
tests:
  - { in: [["", ""]], out: [["", ""]] }
  - { in: [["ab", "ba", "abc", "cba", "bca", "cab", "x"]], out: [["ab", "ba"], ["abc", "cba", "bca", "cab"], ["x"]] }
  - { in: [["listen", "silent", "enlist", "google", "gogole", "cat", "act", "tac", "dog", "god", "odg", "bird"]], out: [["listen", "silent", "enlist"], ["google", "gogole"], ["cat", "act", "tac"], ["dog", "god", "odg"], ["bird"]] }
  - { in: [["aab", "aba", "baa", "abb", "bab"]], out: [["aab", "aba", "baa"], ["abb", "bab"]] }
  - { in: [["abc", "def", "ghi"]], out: [["abc"], ["def"], ["ghi"]] }
  - { in: [["zz", "z", "zzz", "zz"]], out: [["zz", "zz"], ["z"], ["zzz"]] }
  - { in: [["a", "a", "abaa", "", "", "bbc", "", "", "abc", "b", "aab", "ba", "bb", "", "", "", "b", "cc", "caba", "ac", "bcb", "abcc", "cbb", "cbc", "a", "b", "bbac", "", "ccca", "a", "acc", "a", "ca", "cc", "b", "", "cbcb", "bcb", "ca", "bbb", "ccaa", "", "acbb", "aca", "ac", "aa", "", "aaac", "bccc", "ac", "cbbc", "c", "", "bacb", "cbaa", "aabc", "aa", "bcc", "", "c", "", "ccbb", "", "", "ac", "aa", "c", "aba", "bc", "b", "a", "caa", "b", "a", "", "cbc", "aa", "", "aa", "aa", "bc", "c", "a", "ba", "bcb", "b", "acc", "", "a", "caba", "aaa", "abb", "bcaa", "acbc", "c", "", "ab", "b", "bbcb", "cbc", "cca", "bbcc", "", "a", "aaa", "", "b", "baaa", "b", "ac", "ba", "aaa", "ac", "", "bab", "", "cb", "b", "cbc", "c", "ccc", "bba", "cccc", "", "bcbb", "a", "bbbb", "aaac", "", "bbaa", "b", "", "", "", "ccc", "bcac", "", "b", "", "acac", "b", "bcab", "", "", "", "bcb", "cbbc", "c", "a", "ab", "acbb", "acb", "bb", "c", "aac", "", "ca", "bb", "c", "aaab", "ca", "", "ca", "ccbc", "acb", "bccb", "cb", "aac", "", "aa", "", "", "cab", "", "cbac", "ca", "aa", "cba", "cab", "c", "b", "b", "a", "a", "aba", "aab", "c", "abb", "b", "aba", "cca", "cb", "aaa", "bacb", "cba", "a", "", "a", "ba", "cbaa", "baca", "caca", "babc", "", "c", "bcb", "", "b", "cbaa", "ccac", "abc", "a", "a", "aacb", "bb", "a", "", "ac", "accb", "c", "caab", "cb", "aac", "bbbc", "bc", "c", "", "cbb", "bb", "a", "", "cc", "cbaa", "caab", "cc", "cab", "abc", "aaab", "bacb", "cca", "aa", "", "aac", "a", "aaac", "baab", "bb", "bbba", "", "ac", "a", "c", "", "c", "cbc", "c", "aba", "aa", "bbca", "bb", "cca", "aa", "cbc", "", "b", "b", "bbba", "bccc", "a", "c", "bac", "a", "aac", "bbcc", "bbc", "b", "", "a", "cbba", "bccc", "aabc", "b", "aa", "", "ba", "bc", "abb", "bc", "ccaa", "ccba", "baa", "acc", "", "abbb", "bac", "ab", "bbb", "", "aba", "ba"]], out: [["a", "a", "a", "a", "a", "a", "a", "a", "a", "a", "a", "a", "a", "a", "a", "a", "a", "a", "a", "a", "a", "a", "a", "a", "a"], ["abaa", "baaa", "aaab", "aaab"], ["", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", ""], ["bbc", "bcb", "cbb", "bcb", "bcb", "bcb", "bcb", "cbb", "bbc"], ["abc", "acb", "acb", "cab", "cba", "cab", "cba", "abc", "cab", "abc", "bac", "bac"], ["b", "b", "b", "b", "b", "b", "b", "b", "b", "b", "b", "b", "b", "b", "b", "b", "b", "b", "b", "b", "b", "b"], ["aab", "aba", "aba", "aab", "aba", "aba", "baa", "aba"], ["ba", "ba", "ab", "ba", "ab", "ba", "ba", "ab", "ba"], ["bb", "bb", "bb", "bb", "bb", "bb", "bb"], ["cc", "cc", "cc", "cc"], ["caba", "cbaa", "aabc", "caba", "bcaa", "cbaa", "baca", "cbaa", "aacb", "caab", "cbaa", "caab", "aabc"], ["ac", "ca", "ca", "ac", "ac", "ac", "ac", "ac", "ca", "ca", "ca", "ca", "ac", "ac"], ["abcc", "acbc", "bcac", "cbac", "accb", "ccba"], ["cbc", "bcc", "cbc", "cbc", "cbc", "cbc", "cbc"], ["bbac", "acbb", "bacb", "bcab", "acbb", "bacb", "babc", "bacb", "bbca", "cbba"], ["ccca", "ccac"], ["acc", "acc", "cca", "cca", "cca", "cca", "acc"], ["cbcb", "cbbc", "ccbb", "bbcc", "cbbc", "bccb", "bbcc"], ["bbb", "bbb"], ["ccaa", "acac", "caca", "ccaa"], ["aca", "caa", "aac", "aac", "aac", "aac", "aac"], ["aa", "aa", "aa", "aa", "aa", "aa", "aa", "aa", "aa", "aa", "aa", "aa"], ["aaac", "aaac", "aaac"], ["bccc", "ccbc", "bccc", "bccc"], ["c", "c", "c", "c", "c", "c", "c", "c", "c", "c", "c", "c", "c", "c", "c", "c", "c", "c"], ["bc", "bc", "cb", "cb", "cb", "cb", "bc", "bc", "bc"], ["aaa", "aaa", "aaa", "aaa"], ["abb", "bab", "bba", "abb", "abb"], ["bbcb", "bcbb", "bbbc"], ["ccc", "ccc"], ["cccc"], ["bbbb"], ["bbaa", "baab"], ["bbba", "bbba", "abbb"]] }
---
给定一个字符串数组 `strs`，把其中的**字母异位词**放到同一组里，返回所有的组。

两个字符串互为字母异位词，是指它们用到的字母和每个字母的次数都完全相同，只是排列顺序可以不同，比如 `"eat"` 和 `"tea"`。

组与组之间、组内字符串之间的顺序都不限。

**约束**

- `1 <= strs.length <= 10^4`
- `0 <= strs[i].length <= 100`
- `strs[i]` 只包含小写英文字母

**提示**：给每个字符串算一个「异位词都相同」的键（比如排序后的字符串，或者 26 个字母的计数），用哈希表按键分组。
