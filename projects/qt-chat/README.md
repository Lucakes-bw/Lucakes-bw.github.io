# QtChat（客户端开发专栏综合实战）

一个完整的聊天程序：C 写的服务器（epoll，运行在 Docker 的 Ubuntu 中）和 Qt 6 写的桌面客户端。

- `server/`：聊天服务器 `chatd.c` 和 `Dockerfile`
- `client/`：Qt 客户端；`deploy-macos.sh` 打包 macOS 版
- `pack.sh`：打包成 `source/downloads/qt-chat.zip`，网站上发布的就是这个 zip

运行：

```bash
# 服务器
cd server && docker build -t chatd . && docker run --rm -p 9000:9000 --name chatd chatd

# 客户端（另开一个终端）
cd client && cmake -B build -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt && cmake --build build
./build/QtChat.app/Contents/MacOS/QtChat --user lucky      # Linux、Windows 上是 ./build/QtChat
```

`-DCHAT_DEVTOOLS=ON` 会编译 `devtools.cpp`：按环境变量 `CHAT_SCRIPT` 中的脚本驱动界面，专栏中的截图都是这样自动生成的。发布版不要打开它。

这个目录不在 `source/` 中，Hexo 不会处理它。
