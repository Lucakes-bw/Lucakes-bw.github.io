---
id: 103
title: 重构挑战：日志过滤与脱敏管线
difficulty: medium
tags: [重构挑战, 设计模式, 责任链模式]
related: [dp-chain, dp-project-minilog-pipeline]
langs: [cpp]
function: processLog
params: { level: int, module: string, msg: string, minLevel: int, denyModule: string }
returns: string
starter: |
  #include <cctype>
  #include <string>
  using namespace std;

  // 所有过滤与改写规则全挤在一个函数里，每加一条规则都要回来改主流程
  class Solution {
  public:
      string processLog(int level, string module, string msg, int minLevel, string denyModule) {
          if (level < minLevel) return "";
          if (module == denyModule) return "";

          size_t pos = msg.find("token=");
          if (pos != string::npos) {
              size_t end = pos + 6;
              while (end < msg.size() && (isalnum(static_cast<unsigned char>(msg[end])) || msg[end] == '_')) {
                  ++end;
              }
              if (end > pos + 6) {
                  msg.replace(pos + 6, end - (pos + 6), "***");
              }
          }
          return msg;
      }
  };
checks:
  - forbid: '\blevel\s*<\s*minLevel\b'
    msg: 不能在 processLog 里直接写死 level < minLevel 判断，请拆成独立的级别过滤类
  - forbid: '\bmodule\s*==\s*denyModule\b'
    msg: 不能在 processLog 里直接写死 module == denyModule 判断，请拆成独立的模块过滤类
  - require: 'virtual[^;{]*\bapply\s*\([^)]*\)[^;{]*=\s*0\s*;'
    msg: 需要一个过滤器接口：带纯虚函数 apply(...) 的抽象类
  - require: '\bclass\s+\w+\s*(final\s*)?:\s*public\s+\w+'
    min: 4
    msg: 每种处理阶段写成一个类，public 继承过滤器接口（级别、模块、脱敏、截断共 4 个）
examples:
  - in: [1, "auth", "login ok", 2, "metrics"]
    out: ""
    explain: 日志级别 1 低于最低门槛 2，被级别过滤阶段直接拦截返回空字符串 `""`。
  - in: [3, "metrics", "cpu=80%", 2, "metrics"]
    out: ""
    explain: 模块 `"metrics"` 命中拦截名单，返回空字符串 `""`。
  - in: [2, "auth", "user login token=Abc123_z ok", 2, "metrics"]
    out: "user login token=*** ok"
    explain: 通过前两关后，第三关把 `token=` 后连续的字母/数字/下划线替换成 `***`。
  - in: [2, "order", "12345678901234567890123456789012345", 2, "metrics"]
    out: "12345678901234567890123456789012..."
    explain: 新增的长消息截断阶段：先做脱敏，若结果长度超过 32 个字符，截断为前 32 个字符加 `"..."`。
tests:
  - { in: [0, "auth", "trace msg", 1, "cron"], out: "" }
  - { in: [2, "cron", "job started", 1, "cron"], out: "" }
  - { in: [2, "auth", "token=secret99", 2, "cron"], out: "token=***" }
  - { in: [2, "auth", "token=", 2, "cron"], out: "token=" }
  - { in: [2, "auth", "12345678901234567890123456789012", 2, "cron"], out: "12345678901234567890123456789012" }
  - { in: [2, "auth", "prefix_token=VeryLongSecretTokenValue12345678", 2, "cron"], out: "prefix_token=***" }
  - { in: [4, "pay", "123456789012345678901234567890 token=XYZ123", 2, "cron"], out: "123456789012345678901234567890 t..." }
  - { in: [2, "pay", "", 2, "cron"], out: "" }
---

这是一道**重构挑战**：编辑器里的 `processLog` 虽然能跑，但所有过滤与脱敏规则全硬编码在同一个函数里。随着业务要求不断加条件，每次都要回头修改主流程。

你要完成两件事：

**1. 新增第 4 个管线阶段：超长截断**

- 管线按顺序执行：**① 级别过滤 → ② 拒绝模块过滤 → ③ `token=` 脱敏 → ④ 超长截断**；
- 如果前一步已拦截（返回空串 `""`），后续阶段不再执行；
- 若经过脱敏后的消息长度 **严格大于 32 个字符**，截断为前 32 个字符并拼接 `"..."`（恰好 32 个字符保持不变）。

**2. 重构成不纯责任链 / 过滤器管线**

```uml
label: 重构后的管线：Solution 按顺序持有多个 LogFilter，4 个具体阶段各自实现 apply
classes:
  - { id: Solution, x: 20, y: 20, w: 280, ops: ['+ processLog(...) : string'] }
  - { id: LogFilter, stereo: interface, x: 420, y: 20, w: 230, ops: ['*+ apply(ctx: LogContext&) : bool'] }
  - { id: LevelFilter, x: 300, y: 175, w: 105 }
  - { id: ModuleFilter, x: 418, y: 175, w: 108 }
  - { id: RedactFilter, x: 540, y: 175, w: 108 }
  - { id: TruncateFilter, x: 662, y: 175, w: 118 }
rels:
  - { from: Solution, to: LogFilter, type: depend, label: '顺序执行' }
  - { from: LevelFilter, to: LogFilter, type: realize, mid: 130 }
  - { from: ModuleFilter, to: LogFilter, type: realize, mid: 130 }
  - { from: RedactFilter, to: LogFilter, type: realize, mid: 130 }
  - { from: TruncateFilter, to: LogFilter, type: realize, mid: 130 }
```

- 定义一个抽象过滤器接口（如 `LogFilter`），包含纯虚函数 `virtual bool apply(LogContext& ctx) const = 0;`（返回 `false` 表示拦截，`true` 表示放行并允许就地修改 `ctx.msg`）；
- 把 4 个阶段各自写成 `public` 继承该接口的具体类；
- `processLog` 只负责构造上下文和 `vector<unique_ptr<LogFilter>>` 管线，依次执行 `apply`，任意一关返回 `false` 即刻返回 `""`。

提交时除了跑测试用例，还会**检查代码结构**：不能再在 `processLog` 里直接写死 `level < minLevel` 和 `module == denyModule`，且必须有带纯虚函数 `apply(...)` 的接口和至少 4 个继承它的具体类。
