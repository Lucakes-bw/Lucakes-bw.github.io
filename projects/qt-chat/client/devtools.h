#pragma once
class MainWindow;

// 开发用：按环境变量 CHAT_SCRIPT 中的脚本驱动界面，用于自动测试和截图。发布的程序中不包含它。
// 脚本是用分号隔开的命令：wait 毫秒; open 联系人; type 文字; send; shot 文件; dialog 文件; quit
namespace DevTools {
void runScriptFromEnv(MainWindow *window);
}
