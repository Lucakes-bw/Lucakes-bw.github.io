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
