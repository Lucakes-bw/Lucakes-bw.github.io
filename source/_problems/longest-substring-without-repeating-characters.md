---
id: 16
title: 无重复字符的最长子串
difficulty: medium
tags: [字符串, 哈希表, 滑动窗口]
related: [ds-hash-table]
leetcode: https://leetcode.cn/problems/longest-substring-without-repeating-characters/
function: lengthOfLongestSubstring
params: { s: string }
returns: int
examples:
  - in: ["abcabcbb"]
    out: 3
    explain: "最长的无重复子串是 \"abc\"，长度为 3。"
  - in: ["bbbbb"]
    out: 1
  - in: ["pwwkew"]
    out: 3
    explain: "答案是 \"wke\"，长度为 3。注意 \"pwke\" 是子序列，不是子串。"
tests:
  - { in: [""], out: 0 }
  - { in: [" "], out: 1 }
  - { in: ["au"], out: 2 }
  - { in: ["dvdf"], out: 3 }
  - { in: ["abba"], out: 2 }
  - { in: ["tmmzuxt"], out: 5 }
  - { in: ["a b!c a"], out: 5 }
  - { in: ["0123456789abcdefghij0"], out: 20 }
  - { in: ["bcwfcrmehwxoehvkvnlullbvleywtatwbzuamrufyypozeqlahlwaklxtnzicnkysqketggghrglylomgwrpocjqyfxwugolhzjdbphgfctibelnuqahtdbqistcihrmtznllvfitmsklzjanruusrtpjbbmkgamyztavuaiebyzuqndxtyoqngyldqohigmwslhldfbihseptkeaxaqbhpvijpnheqnfcjkllvxhbsakiljalelrropnwltadoelzseywkbvbulrmycgzisfqitgbucxlfvznvpghdcjcnncabgtignvgvulrmkqmuujhgkzdbpbygkbxakeslbbylfllaredtprbmjvusuzmriyfmxcrsghwostvghgowdljosupcusbcbmdvglcfamrfsrjhxbywqheuhfqbczwzvqscwhcdcsijqgxmnifahocagmlohtbnuvfsktavzwavpddmywwxiqdxppytrlpytkfehzimscjzbswzxidtvzgnmzdaoqlvzwhnbuftzcjjitprqjyhfrxoirrdjrybajofeealmotyoqtjooxobcxfjgezbaskpkdjkusxgciccslzprrizckkpywntpmgzjqscgzutadjdbfxvjjsonqwffkgoganzdvjnxaoqqczvgygmspugwsuxjpvqsizayxjooavjkuezdujtebrnqjnopztqqbanbgmfosjmaqzzddnomjzioycrbkbwpnklecofjiaznhgcdsbjddrxuyuzrkimzvizctqvkwxsoggbzqyfdkpxzafoddeloysgjoninmumxjqkdxrtixgjaccgozkkruntmwrwfyphunmzbkqaeqedixbsfozoqzoeooyqjwmqkzputeakmibbezkbqiphhsfozvwdorbrwjuchppvtuhctjnsnfbhblchgpuwtejvrfarkdqpqnxscqctazvctqyuxvdqcivpzhvcqitsgpjpsgcfcmxqhitcgmfqvzilqzslnexwmxiytqbqtmiudbvhertxoedvldlerbuqbsojzjyztvdgwyefbbongdjncdnfjufbvucnqkdgosqlddbkavxueipyubhwjaigdwqyxngejjsmjzqrvnlspkzcppjqnbtowbjtyefgilaheophcigxbtfratasisftjjuquoiwfgyhuvvidicwfibodnbchsqzhefccisqzkqyacucudshimokswdwtjrslztwyjkgleriqnosuclyuogcsaxcoplnmnmxesvxitefwvmhzwlalkmxycdopkobccdtqjpjxcvwgpjmvhkjumhixflqxmbcagbcvzrvvsfmjanxzmniqckarvwamflfhocjauniizwzpkjoclougwrfbxzneuoudplswpbwwnxalvcreopggbhufgyakgniucahwghkvnwistmxzncuqiegdpgnwwhaizqbeqwidmxmyotmrzegutxjnrkrsqadxpoeugzhmxektjpodfiegamkspiwuzdnqcvedidizwhbxtlepkvospfsyfxzknxdlaiekeqtfsxncztkcfaxdfvzobyoknhdtjjtlebdkbwtuiglxwksovjasbbfbiumlypzxeruaxwnpnocdjjkfhlelhvjpgvrodmcucixvwfglyojayuduugsblknsigmbmsvshrrfonagjsuxddnnmqgkbzhpmucjteqzgwkovikmgzzfpnoevijsapfldvzzlqyrmpcvaepcuqwbbhkxpuivobmyccmbvdmzkrgnlpsglknujuhtedqpjscieywsstsnffiykwmtizadullgpqfspjsafmorkynbmaxbtrudemphbepmsncvmapnqvkbwpvzknywajcienhivfkbiawiftxgubzyxlvgcumnitmwvdasfeygbbatsajzjzvgivihlygygbbemdvtjkouwbmrrrezheaozicaatpxjkdkhlkvwkx"], out: 17 }
---
给定字符串 `s`，求其中**不含重复字符**的最长**子串**的长度。

子串是原字符串中连续的一段。

**约束**

- `0 <= s.length <= 5 * 10^4`
- `s` 由英文字母、数字、符号和空格组成

**提示**：维护一个窗口 `[left, right]`，保证窗口内没有重复字符。右端每加进一个字符，如果它已经在窗口里，就把左端移到它上次出现位置的后面。
