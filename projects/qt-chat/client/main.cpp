#include <QApplication>
#include <QCommandLineParser>
#include <QLibraryInfo>
#include <QMessageBox>
#include <QTranslator>
#include "chatclient.h"
#include "logindialog.h"
#include "mainwindow.h"
#ifdef CHAT_DEVTOOLS
#include "devtools.h"
#endif

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("lucky-notes");
    QApplication::setApplicationName("QtChat");             // 决定数据和设置保存的位置

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

    // 可以用命令行参数跳过登录框：QtChat --user lucky --server 127.0.0.1:9000
    QCommandLineParser parser;
    parser.addOption({"user", "用户名", "name"});
    parser.addOption({"server", "服务器地址", "host:port", "127.0.0.1:9000"});
    parser.process(app);

    QString user = parser.value("user");
    QString server = parser.value("server");
    if (user.isEmpty()) {
        LoginDialog login;
        if (login.exec() != QDialog::Accepted)              // 程序刚启动，用 exec 是安全的
            return 0;
        user = login.user();
        server = login.host() + ":" + QString::number(login.port());
    }

    ChatClient client;
    MainWindow window(&client);
    if (!client.start(user, server.section(':', 0, 0), quint16(server.section(':', 1, 1).toUInt()))) {
        QMessageBox::critical(nullptr, "QtChat", "无法打开本地的消息数据库。");
        return 1;
    }
    window.show();
#ifdef CHAT_DEVTOOLS
    DevTools::runScriptFromEnv(&window);
#endif
    return app.exec();
}
