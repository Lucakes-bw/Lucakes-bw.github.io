---
title: 实战：打包与发布
date: 2026-10-31 12:00:00
updated: 2026-10-31 12:00:00
categories:
  - 客户端开发
  - 综合实战
tags:
  - Qt
  - macdeployqt
  - 发布
order: 4
description: 编译出来的 QtChat.app 只有 260 KB，因为它引用的是 /opt/homebrew 下的 Qt，拷到别人的 Mac 上无法运行。macdeployqt 把 Qt 的框架和插件复制进包里，改写库的查找路径，包变成了 83 MB，其中还混进了我们根本没用的 QML、Quick 和虚拟键盘插件，打包时还报了错。运行时用 DYLD_PRINT_LIBRARIES 检查：从包内加载了 31 个库，从 /opt/homebrew 加载了 0 个。但有一个问题只有截图才能发现：打包后 QLibraryInfo 指向了一个不存在的目录，Qt 的中文翻译找不到了，对话框的按钮变回了 Cancel、No、Yes。最后写一个打包脚本：删掉多余的框架，带上翻译文件，重新签名，生成 dmg，包从 83 MB 降到 72 MB，dmg 32 MB。
top_group_list: true
disableNunjucks: true
---

> **本文目标**：理解为什么编译出来的程序不能直接给别人用；会用 `macdeployqt` 打包 macOS 程序，看懂它做了什么；会用 `otool -L` 和 `DYLD_PRINT_LIBRARIES` 验证打包结果不再依赖开发机；知道打包后容易漏掉的东西（翻译文件），以及签名、精简包的大小；了解 Windows 和 Linux 上对应的工具。
>
> **前置知识**：第一章《环境与第一个 Qt 程序》第 5 节（程序依赖的库和平台插件）；Linux 基础专栏《静态库与动态库》。
>
> **实验环境**：macOS 27.0（Apple M6），Homebrew 安装的 Qt 6.12.0，Release 构建。

## 1. 编译出来的程序不能直接给别人

第一章第 5 节已经看到，Qt 程序依赖 Qt 的动态库和平台插件。QtChat 的 `CMakeLists.txt` 中，可执行文件加了 `MACOSX_BUNDLE`，编译出来的是一个 macOS 的**应用程序包**（bundle）`QtChat.app`，双击就能运行：

```cmake
qt_add_executable(QtChat MACOSX_BUNDLE WIN32 ${SOURCES})

set_target_properties(QtChat PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.lucakes-bw.qtchat
    MACOSX_BUNDLE_BUNDLE_NAME QtChat
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)
```

（`WIN32` 在 Windows 上表示这是一个图形界面程序，启动时不打开命令行窗口；在其他平台上被忽略。）

`.app` 其实是一个目录：

```
QtChat.app/
└── Contents/
    ├── Info.plist          程序的信息：名字、标识符、版本……（由上面的属性生成）
    └── MacOS/
        └── QtChat          可执行文件
```

```bash
du -sh QtChat.app
otool -L QtChat.app/Contents/MacOS/QtChat | grep -i qt
```

```
260K	QtChat.app
	/opt/homebrew/opt/qtbase/lib/QtWidgets.framework/Versions/A/QtWidgets (compatibility version 6.0.0, current version 6.12.0)
	/opt/homebrew/opt/qtbase/lib/QtNetwork.framework/Versions/A/QtNetwork (...)
	/opt/homebrew/opt/qtbase/lib/QtSql.framework/Versions/A/QtSql (...)
	/opt/homebrew/opt/qtbase/lib/QtGui.framework/Versions/A/QtGui (...)
	/opt/homebrew/opt/qtbase/lib/QtCore.framework/Versions/A/QtCore (...)
```

整个包只有 260 KB，因为它用的 Qt 库都在 `/opt/homebrew` 下面，以绝对路径记录在可执行文件中。拷到一台没有装 Homebrew 的 Qt 的 Mac 上，系统按这些路径找不到库，程序一启动就会崩溃。

**发布**一个程序，就是把它运行时需要的一切都带上：Qt 的库、插件、Qt 依赖的其他库、翻译文件，并让程序到包里面去找它们，而不是到开发机的路径去找。

## 2. macdeployqt

Qt 为 macOS 提供了 `macdeployqt`：

```bash
macdeployqt QtChat.app
```

它做了三件事：

1. 分析可执行文件依赖哪些 Qt 库（以及这些库又依赖哪些库），把它们复制到 `Contents/Frameworks/`；
2. 把需要的插件（平台、样式、数据库驱动、图片格式……）复制到 `Contents/PlugIns/`；
3. 修改所有库的查找路径，从 `/opt/homebrew/...` 改成相对于程序自己的 `@executable_path/../Frameworks/...`。

运行了 7.5 秒，输出了几行错误：

```
ERROR: Cannot resolve rpath "@rpath/QtVirtualKeyboardQml.framework/Versions/A/QtVirtualKeyboardQml"
ERROR: Cannot resolve rpath "@rpath/QtVirtualKeyboard.framework/Versions/A/QtVirtualKeyboard"
```

QtChat 并没有用到虚拟键盘。看看包里现在有什么：

```
 83M	QtChat.app

Frameworks/:
QtCore.framework   QtDBus.framework   QtGui.framework   QtNetwork.framework
QtOpenGL.framework QtQml.framework    QtQmlMeta.framework QtQmlModels.framework
QtQmlWorkerScript.framework  QtQuick.framework  QtSql.framework  QtWidgets.framework
libb2.1.dylib  libbrotlicommon.1.dylib  libcrypto.4.dylib  libdbus-1.3.dylib
libfreetype.6.dylib  libglib-2.0.0.dylib  libharfbuzz.0.dylib  libicudata.79.dylib
libicui18n.79.dylib  libicuuc.79.dylib  libjpeg.8.dylib  libpng16.16.dylib  libssl.4.dylib
……（共 40 多个）

PlugIns/:
platforms/libqcocoa.dylib            styles/libqmacstyle.dylib
sqldrivers/libqsqlite.dylib          platforminputcontexts/libqtvirtualkeyboardplugin.dylib
tls/（3 个）   imageformats/（11 个）   iconengines/libqsvgicon.dylib   networkinformation/（2 个）
```

包从 260 KB 变成了 83 MB。除了 QtChat 用到的 Core、Gui、Widgets、Network、Sql，还有两类「不速之客」：

- **QML、Quick 等框架**：Homebrew 安装的是完整的 Qt，其中有一个虚拟键盘的输入法插件（`platforminputcontexts`），`macdeployqt` 把所有的输入法插件都复制了进来，而这个插件依赖 QML 和 Quick，于是它们也被复制了进来。插件依赖的 `QtVirtualKeyboard` 框架本身又没找到，就是上面那几行错误。
- **glib、ICU、harfbuzz、dbus 等第三方库**：Homebrew 编译 Qt 时，使用的是 Homebrew 中的这些库，而不是 Qt 自带的版本，它们也都要带上。其中最大的是 ICU 的数据文件 `libicudata`，一个就有 32 MB。用 Qt 官方安装器的 Qt 打包，第三方依赖一般会少得多。

`macdeployqt` 的选项不多，只能用 `-no-plugins` 关掉全部插件，不能按名字排除某一个。第 4 节在打包后手动删除多余的部分。

## 3. 验证：真的不依赖开发机了吗

### 3.1 静态检查

```bash
otool -L QtChat.app/Contents/MacOS/QtChat | grep -i qt
```

```
	@executable_path/../Frameworks/QtWidgets.framework/Versions/A/QtWidgets (...)
	@executable_path/../Frameworks/QtNetwork.framework/Versions/A/QtNetwork (...)
	@executable_path/../Frameworks/QtSql.framework/Versions/A/QtSql (...)
	@executable_path/../Frameworks/QtGui.framework/Versions/A/QtGui (...)
	@executable_path/../Frameworks/QtCore.framework/Versions/A/QtCore (...)
```

`@executable_path` 是可执行文件所在的目录，也就是 `Contents/MacOS/`。再检查包里**所有**的可执行文件和库，看有没有任何一个还引用 `/opt/homebrew`：

```bash
find QtChat.app -type f \( -name "*.dylib" -o -perm -111 \) | while read f; do
    otool -L "$f" | grep -q "/opt/homebrew" && echo "$f"
done
```

没有输出：一个都没有。

### 3.2 运行时检查

静态检查只能看到直接的依赖，插件是运行时才加载的。macOS 的动态链接器 dyld 有一个环境变量 `DYLD_PRINT_LIBRARIES`，设置后会打印加载的每一个库：

```bash
DYLD_PRINT_LIBRARIES=1 QT_DEBUG_PLUGINS=1 ./QtChat.app/Contents/MacOS/QtChat --user packtest 2> run.log
```

dyld 每加载一个库，就打印一行 `dyld[进程号]: <UUID> 路径`。按路径分类统计：

```bash
grep "^dyld\[" run.log | grep -c "QtChat.app/"                  # 包内
grep "^dyld\[" run.log | grep -cE " /(System|usr/lib)/"         # 系统
grep "^dyld\[" run.log | grep -c "/opt/homebrew"                # Homebrew
```

```
31
1167
0
```

程序运行时从包内加载了 31 个库，系统自带的（AppKit 等）1167 个，**从 `/opt/homebrew` 加载的是 0 个**。`QT_DEBUG_PLUGINS` 的输出中，实际加载的插件只有三个：

```
PlugIns/platforms/libqcocoa.dylib" loaded library
PlugIns/styles/libqmacstyle.dylib" loaded library
PlugIns/sqldrivers/libqsqlite.dylib" loaded library
```

平台插件、macOS 样式、SQLite 驱动，正是 QtChat 需要的。虚拟键盘插件、QML、Quick 都没有被加载，确实是多余的。

### 3.3 一个检查不出来的问题：翻译

库都对了，程序也能运行，但还有一个问题，只有看到界面才会发现。弹出一个标准的确认对话框：

<img src="/img/client/deploy-no-translation.png" alt="打包后的程序：对话框的按钮变成了英文" width="322">

按钮是 **Cancel、No、Yes**。开发时运行，这里是中文的。

QtChat 用第二章 2.1 篇的方法加载 Qt 自带的中文翻译，目录来自 `QLibraryInfo::path(QLibraryInfo::TranslationsPath)`。写一个小程序，看看打包前后这个目录是什么：

```cpp
QString dir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
QTranslator tr;
bool ok = tr.load(QLocale(QLocale::Chinese, QLocale::China), "qtbase", "_", dir);
std::printf("TranslationsPath = %s\n目录存在 %d，加载 qtbase_zh_CN %s\n", qPrintable(QDir::cleanPath(dir)),
            QDir(dir).exists(), ok ? "成功" : "失败");
```

```
== 打包前
TranslationsPath = /opt/homebrew/share/qt/translations
目录存在 1，加载 qtbase_zh_CN 成功
== macdeployqt 之后
TranslationsPath = …/exp18/share/qt/translations
目录存在 0，加载 qtbase_zh_CN 失败
```

Qt 6 是可以搬动位置的（relocatable）：它根据 QtCore 库**当前所在的位置**推算其他目录。打包后 QtCore 在 `xxx.app/Contents/Frameworks/` 中，按 Homebrew 的目录结构往上推算，得到了一个不存在的目录。`macdeployqt` 不会复制翻译文件，`QTranslator::load` 失败了也不报错，于是在**每一个用户的电脑上**，标准按钮都是英文的，而开发者自己的电脑上一切正常。

修正分两步。打包时把翻译文件复制进包里；程序先在包里找，找不到再用 `QLibraryInfo` 的目录（开发时）：

```cpp
// Qt 自带控件的中文翻译（《对话框与输入校验》）。先找程序自己带的（打包后），再找 Qt 安装目录中的（开发时）
QTranslator qtTr;
const QString appDir = QCoreApplication::applicationDirPath();
for (const QString &dir : {appDir + "/../Resources/translations",       // macOS 的 .app 包
                           appDir + "/translations",                    // Windows、Linux：和程序放在一起
                           QLibraryInfo::path(QLibraryInfo::TranslationsPath)}) {
    if (qtTr.load(QLocale::system(), "qtbase", "_", dir)) {
        app.installTranslator(&qtTr);
        break;
    }
}
```

<img src="/img/client/deploy-translation.png" alt="带上翻译文件后：按钮是「取消」「否」「是」" width="336">

这个问题提醒我们：**打包后的程序要在界面上实际检查一遍**，最好是在一台没有装开发环境的电脑上。依赖检查、能不能启动，都发现不了这类问题。

## 4. 打包脚本

把上面的步骤写成一个脚本 `deploy-macos.sh`，每次发布都执行同样的操作：

```sh
#!/bin/sh
# 打包 macOS 版：./deploy-macos.sh <构建目录>
# 生成 <构建目录>/QtChat.app 和 QtChat.dmg
set -e
BUILD=${1:-build}
APP="$BUILD/QtChat.app"
QT=$(qtpaths --install-prefix 2>/dev/null || echo /opt/homebrew/opt/qt)

# 1. 复制 Qt 的框架和插件，修改库的查找路径
"$QT/bin/macdeployqt" "$APP" -verbose=1

# 2. 删除用不到的插件和它们拉进来的框架（Homebrew 版的 Qt 带了虚拟键盘插件，它依赖 QML 和 Quick）
rm -rf "$APP/Contents/PlugIns/platforminputcontexts"
for fw in QtQuick QtQml QtQmlMeta QtQmlModels QtQmlWorkerScript QtOpenGL QtVirtualKeyboard QtVirtualKeyboardQml; do
    rm -rf "$APP/Contents/Frameworks/$fw.framework"
done

# 3. 带上 Qt 自带控件的中文翻译
mkdir -p "$APP/Contents/Resources/translations"
cp "$(qtpaths --query QT_INSTALL_TRANSLATIONS 2>/dev/null || echo /opt/homebrew/share/qt/translations)/qtbase_zh_CN.qm" \
   "$APP/Contents/Resources/translations/"

# 4. 修改过包的内容，签名失效了，重新做一次临时签名（ad-hoc），否则 Apple 芯片的 Mac 拒绝运行
codesign --force --deep --sign - "$APP"

# 5. 生成安装用的磁盘映像
rm -f "$BUILD/QtChat.dmg"
hdiutil create -quiet -volname QtChat -srcfolder "$APP" -ov -format UDZO "$BUILD/QtChat.dmg"
du -sh "$APP" "$BUILD/QtChat.dmg"
```

```
 72M	QtChat.app
 32M	QtChat.dmg
```

```bash
codesign -v QtChat.app && echo "签名校验通过"
```

```
签名校验通过
```

- 删掉多余的框架和插件后，包从 83 MB 降到 72 MB。剩下的大头是 ICU（约 32 MB）和 Qt 的几个核心库（QtGui、QtWidgets、QtCore 各约 5～6 MB）。程序本身只有 224 KB。
- dmg 是压缩过的磁盘映像，32 MB，用户下载后打开，把 QtChat 拖进「应用程序」即可。
- 删掉文件之后，再按 3.2 节的方法运行一次：程序正常启动，`/opt/homebrew` 仍然是 0；3.3 节的对话框显示中文按钮（那张截图就是用这个脚本打出的包截的）。

### 4.1 签名

**签名**是什么？macOS 用代码签名确认一个程序没有被篡改过。在 Apple 芯片的 Mac 上，**所有的可执行代码都必须有签名**，哪怕是一个临时的签名，否则系统拒绝运行。`macdeployqt` 默认会做一次临时签名（ad-hoc，`-` 表示不用任何证书），但第 4 节的脚本在它之后又删除和添加了文件，签名就对不上了，所以要重新签一次。

临时签名只能保证程序在自己的电脑上运行。发给别人时，从网上下载的程序还要经过 macOS 的 **Gatekeeper** 检查：没有用开发者证书签名、没有经过 Apple 公证（notarization）的程序，双击时系统会提示「无法验证开发者」。正式发布需要加入 Apple 开发者计划，用 `macdeployqt` 的 `-sign-for-notarization` 选项签名后提交 Apple 公证。这一步需要付费账号，本文没有实际操作。

## 5. Windows 和 Linux

其他平台的思路一样：把依赖带上，让程序在自己的目录中找到它们。工具不同（本文没有在这两个平台上实际操作）：

| 平台 | 工具 | 结果 |
|---|---|---|
| macOS | `macdeployqt` | `.app` 包，`.dmg` 磁盘映像 |
| Windows | `windeployqt QtChat.exe` | 和 exe 放在同一个目录中的 DLL、插件、翻译；再用 Inno Setup 等工具做成安装程序 |
| Linux | linuxdeploy（加 Qt 插件）、或者打成 Flatpak / Snap | AppImage：一个可以直接运行的文件 |

Windows 上，3.3 节的翻译问题同样存在，`main.cpp` 中的 `appDir + "/translations"` 就是为它准备的：`windeployqt` 默认会把翻译复制到 exe 旁边的 `translations` 目录。

## 6. 小结

- 编译出来的程序以绝对路径引用开发机上的 Qt，不能直接给别人。发布要把库、插件、翻译都带上，并让程序到自己的目录中找它们。
- `macdeployqt` 复制框架和插件、改写库的查找路径。Homebrew 的 Qt 会带进大量第三方库和不需要的插件，包很大。
- 用 `otool -L` 做静态检查，用 `DYLD_PRINT_LIBRARIES` 和 `QT_DEBUG_PLUGINS` 做运行时检查，确认不再依赖开发机。
- 打包后 `QLibraryInfo` 推算出的目录可能不存在，翻译加载失败也不报错。要把翻译文件带进包里，并优先从程序自己的目录加载。最后一定要在界面上检查一遍。
- 修改包的内容后要重新签名。临时签名只够自己用，正式发布需要开发者证书和 Apple 公证。
- 把打包写成脚本，每次发布执行同样的步骤。

## 7. 专栏总结

客户端开发专栏到这里就结束了。五章、二十篇，从一个按钮开始，最后做出了一个能断线重连、消息不丢不重、能打包发给别人的聊天程序：

- **第一章 Qt入门**：事件循环、布局、信号与槽、对象树。这四样是 Qt 的地基，后面的每一章都建立在它们之上。
- **第二章 界面开发**：对话框、Model/View、样式表与深色模式、自定义绘制。
- **第三章 事件与多线程**：界面上的一切都靠事件循环推动，所以耗时的工作必须交给其他线程，结果通过信号送回来。
- **第四章 网络与数据**：TCP 的分帧、断线检测、HTTP、SQLite。网络是不可靠的，`write` 成功不代表对方收到。
- **第五章 综合实战**：协议设计、界面、可靠性、打包。

如果说这个专栏有一条贯穿始终的经验，那就是：**不要相信「看起来正常」**。readyRead 不等于一条消息，「已连接」不等于服务可用，`write` 成功不等于对方收到，「运行正常」不等于没有数据竞争，「能启动」不等于翻译文件带上了。每一个结论都应该用实验来验证，这也是专栏中每一篇都在做的事。

## 练习

1. 给 QtChat 加一个图标：准备一个 1024×1024 的 PNG，用 `iconutil` 生成 `.icns`，在 `CMakeLists.txt` 中设置 `MACOSX_BUNDLE_ICON_FILE` 并把它加入资源。
2. 把 `Info.plist` 中的版本号改成从 Git 的标签生成（`git describe --tags`），每次打包自动更新。
3. 在 `deploy-macos.sh` 中再删掉用不到的 `imageformats` 插件（QtChat 不显示任何图片），包小了多少？删之前怎样确认它们确实用不到？
4. 在 Windows 上用 `windeployqt` 打包 QtChat，统计包的大小和 DLL 的数量，和 macOS 上的结果比较。
