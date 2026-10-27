---
id: 17
title: 找到字符串中所有字母异位词
difficulty: medium
tags: [字符串, 哈希表, 滑动窗口]
leetcode: https://leetcode.cn/problems/find-all-anagrams-in-a-string/
function: findAnagrams
params: { s: string, p: string }
returns: "int[]"
compare: unordered
examples:
  - in: ["cbaebabacd", "abc"]
    out: [0, 6]
    explain: "下标 0 开始的 \"cba\" 和下标 6 开始的 \"bac\" 都是 \"abc\" 的异位词。"
  - in: ["abab", "ab"]
    out: [0, 1, 2]
tests:
  - { in: ["a", "ab"], out: [] }
  - { in: ["a", "a"], out: [0] }
  - { in: ["aaaaa", "aa"], out: [0, 1, 2, 3] }
  - { in: ["abc", "def"], out: [] }
  - { in: ["baa", "aa"], out: [1] }
  - { in: ["abacbabc", "abc"], out: [1, 2, 3, 5] }
  - { in: ["xyzzyx", "zyx"], out: [0, 3] }
  - { in: ["abbabbbaababbaabbbbaabbbaaabbababbabbaaababababbbbaaabbaabbbaaaaaaaabbbbbbabaabaaaaabbabbabbabaaabaabbabaabaabaaaabbbababababbababbbbabaaaabbbbabaabaaabbbbaaaabaaaaaaaabbaabaabbbabbabaaabbbabaaaaaabbababbbbabbabbaaabaaaabbbbbababbabbaabbbbbaaaabbbabbbabbbbbbbbabbabbbbaababbabaabababbbababaabbbababaabaababaabbbbbbbaabaaaaaaaaabbaaabbbaaabaabaababaaabbbaaabaabbbbbaabbababbbaaabbbabbbbababbbbabaaabbbaababbaaabaabbbbbabaaaabbabbbbaabbbbbabaabbbabbaaababbabaaaabbbaaabbbaabbbabbaaababbbbabaabbbbabababbaaaabbabbbababaababbbaabaabbababbabbbbabaabbbaabbabaaabaaabaabbaababbabababaabaabaabbbbaaaaaaabbbbbabbaabbabbbaabaabaabaaaaaaababababbbaaabaaaababbbabbaaababaaaaababaaabaabaaabbbbbbbbabbaaabbbabaabaaabbbbbabababaababbbbabaabbbaaabbbabbaaaaaababbaaaaaababbabbababaaaabaaabbbbaaaaaaaaaabbbabbaaaaababbaaabbaaabbbbbababbaabbaaaababaabbaabaaaaaaabaaaabaabbaaababaabbaaaabbabbbbbbabbbabbaababaababbaabbbabbbabaaababbaabaaaaaaaabbbaabaababababbabababbaaabababbababbaaaabbaabbabbaabbbababababbaaabbbbaabbbabaabababbbabbaaaabaababababbabbabababbaaabbabbabbbabbabaabaabaaaaabbaaaaaaaaaaaaaabbabbaababaabbbaaabaaabbaaaabbaaababbbababbbaabbabbabbaaabbbbaabaabbaaabaabaababbbbbabbbaabbababbbbbabbbabaaabbbabbaabbbabbbbbbababaaabaaabaabbabbaaabbababbbbbbbabababaaaaaabbabbbbbbbbbbbabaabbababbaaaaaabaabbbabaabbabbbabaaaaaaababaabababbbbabaabaabbbaaabaaabbbbbbbababbaabaabaabbbbbabbabbbbbbaabbbabbbbabaaaabbbbbaaaabaabaababbababbbabbaabbbabbaaaabaabaababbbaabbaabbbaababbaaabbabbbbabaabaabbababbaabaaaabaaabbbababaabaaabaabaaabaababaabaababbbbbbbbbabaaabaabaabaaabbaaabbbaaaaaabbbbabaabbababaaabbabbaaaabababababaaabbabbbabbbaaabbbbaaabaababbababaaaababaabaabaaabababbbbabbabababbabbbbaaabbabaabbaaabbbbbbabaabaababbabaababbbbbabbabbbbaabaaaabaaaaabaaaabbabaabbbbbababaaabbbbaaaabbbbbbbaabbbbaaabbbaabbabaabbbabbabaaaabaabbaaabbbbabbbbbbbbbbaaaaabbaaaaabbaaaaaababbbababababaaabbabaabaabaababbababaaaabababbbbbabbbbbbabbbbabbababababaaaababaaabaaaaabbaaaabbaaabbabbabbbbbbbaaaaaabbbbaaaaababaabbbbbabaabaabbbbbaba", "aabab"], out: [6, 7, 10, 22, 23, 24, 25, 34, 35, 36, 38, 39, 41, 48, 49, 50, 51, 52, 58, 65, 73, 74, 75, 81, 82, 91, 93, 96, 98, 101, 102, 103, 105, 106, 111, 117, 119, 132, 136, 142, 143, 144, 147, 148, 153, 165, 166, 167, 169, 171, 180, 182, 183, 188, 194, 195, 209, 210, 211, 217, 230, 238, 241, 267, 268, 273, 274, 275, 276, 277, 285, 286, 287, 294, 295, 296, 297, 299, 300, 301, 302, 303, 304, 314, 324, 325, 326, 327, 328, 329, 333, 334, 337, 338, 340, 341, 342, 343, 344, 346, 347, 351, 352, 355, 364, 372, 373, 374, 391, 393, 394, 399, 400, 403, 404, 405, 408, 416, 420, 421, 436, 437, 444, 445, 446, 448, 453, 457, 461, 462, 463, 474, 475, 476, 478, 485, 486, 494, 498, 499, 502, 503, 511, 512, 513, 514, 515, 521, 523, 525, 538, 539, 546, 549, 551, 555, 558, 560, 561, 563, 564, 570, 572, 573, 574, 575, 577, 578, 580, 586, 592, 600, 603, 610, 612, 613, 615, 616, 625, 626, 628, 634, 635, 642, 649, 650, 651, 653, 654, 655, 661, 662, 663, 665, 668, 669, 672, 673, 684, 685, 686, 687, 692, 693, 694, 697, 698, 706, 708, 709, 710, 711, 712, 719, 720, 725, 726, 727, 733, 734, 740, 743, 744, 750, 759, 760, 767, 768, 773, 782, 788, 789, 794, 797, 798, 799, 800, 801, 802, 803, 804, 805, 815, 818, 819, 820, 824, 825, 826, 827, 829, 830, 832, 847, 849, 850, 851, 852, 854, 855, 856, 857, 859, 860, 861, 864, 865, 880, 882, 883, 884, 885, 886, 887, 888, 891, 902, 904, 906, 909, 911, 920, 925, 927, 928, 929, 930, 932, 939, 943, 944, 945, 947, 948, 957, 958, 961, 962, 963, 966, 970, 978, 980, 984, 985, 986, 987, 998, 999, 1000, 1001, 1002, 1010, 1011, 1016, 1017, 1018, 1019, 1021, 1031, 1035, 1036, 1037, 1038, 1039, 1052, 1053, 1054, 1056, 1057, 1063, 1064, 1065, 1066, 1079, 1080, 1084, 1086, 1087, 1088, 1089, 1090, 1095, 1096, 1100, 1101, 1102, 1103, 1104, 1107, 1108, 1109, 1110, 1111, 1113, 1126, 1133, 1134, 1135, 1136, 1142, 1144, 1146, 1147, 1148, 1149, 1152, 1153, 1155, 1156, 1157, 1170, 1185, 1187, 1188, 1194, 1209, 1210, 1212, 1216, 1219, 1221, 1225, 1226, 1227, 1228, 1229, 1243, 1245, 1246, 1252, 1253, 1268, 1269, 1271, 1277, 1278, 1285, 1291, 1292, 1294, 1301, 1309, 1310, 1311, 1312, 1313, 1314, 1315, 1323, 1324, 1325, 1327, 1332, 1333, 1337, 1338, 1350, 1352, 1354, 1355, 1357, 1385, 1389, 1395, 1400, 1401, 1403, 1404, 1405, 1417, 1425, 1426, 1431, 1432, 1434, 1435, 1436, 1443, 1444, 1451, 1452, 1455, 1456, 1457, 1458, 1459, 1467, 1468, 1469, 1471, 1473, 1479, 1481, 1489, 1490, 1496, 1497, 1498, 1499, 1502, 1505, 1506, 1509, 1512, 1513, 1514, 1515, 1516, 1517, 1518, 1520, 1521, 1522, 1534, 1536, 1539, 1540, 1542, 1543, 1546, 1547, 1548, 1549, 1550, 1551, 1552, 1556, 1561, 1567, 1568, 1570, 1574, 1575, 1577, 1578, 1579, 1583, 1584, 1588, 1589, 1591, 1593, 1595, 1596, 1598, 1599, 1600, 1610, 1611, 1612, 1617, 1618, 1621, 1622, 1623, 1629, 1630, 1635, 1636, 1637, 1638, 1639, 1641, 1642, 1645, 1647, 1648, 1660, 1670, 1671, 1672, 1673, 1676, 1677, 1679, 1680, 1681, 1682, 1683, 1691, 1692, 1693, 1695, 1696, 1697, 1702, 1703, 1704, 1705, 1721, 1737, 1738, 1741, 1742, 1751, 1752, 1754, 1755, 1760, 1763, 1777, 1778, 1779, 1785, 1788, 1789, 1798, 1804, 1806, 1807, 1808, 1809, 1810, 1826, 1830, 1831, 1832, 1833, 1837, 1838, 1839, 1840, 1846, 1853, 1855, 1857, 1858, 1860, 1861, 1862, 1865, 1866, 1867, 1869, 1870, 1872, 1873, 1874, 1880, 1881, 1886, 1887, 1912, 1914, 1916, 1917, 1922, 1923, 1924, 1926, 1933, 1934, 1935, 1936, 1939, 1940, 1941, 1942, 1943, 1944, 1945, 1958, 1963, 1968, 1973, 1974, 1975, 1976, 1984, 1985, 1986, 1988] }
---
给定两个字符串 `s` 和 `p`，找出 `s` 中所有是 `p` 的**字母异位词**的子串，返回这些子串的起始下标，顺序不限。

字母异位词指字母种类和次数都相同、只是顺序可能不同的字符串。

**约束**

- `1 <= s.length, p.length <= 3 * 10^4`
- `s` 和 `p` 只包含小写英文字母

**提示**：窗口长度固定为 `p.length`。用 26 个计数表示窗口内每个字母的个数，窗口每滑动一格，只需要改两个计数。
