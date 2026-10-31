#include "devtools.h"
#include <QApplication>
#include <QMessageBox>
#include <QTimer>
#include <cstdio>
#include "mainwindow.h"

void DevTools::runScriptFromEnv(MainWindow *window)
{
    QStringList steps = qEnvironmentVariable("CHAT_SCRIPT").split(';', Qt::SkipEmptyParts);
    int delay = 0;
    for (QString step : steps) {
        step = step.trimmed();
        QString cmd = step.section(' ', 0, 0), arg = step.section(' ', 1);
        if (cmd == "wait") {
            delay += arg.toInt();
            continue;
        }
        QTimer::singleShot(delay, window, [window, cmd, arg] {
            if (cmd == "open") window->openChat(arg);
            else if (cmd == "type") window->setDraft(arg);
            else if (cmd == "send") window->sendDraft();
            else if (cmd == "shot") window->grab().save(arg);
            else if (cmd == "quit") qApp->quit();
            else if (cmd == "dialog") {                      // 弹出一个标准对话框并截图，检查翻译
                auto *box = new QMessageBox(QMessageBox::Question, "QtChat", "要清空这个会话的聊天记录吗？",
                                            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, window);
                box->open();
                QTimer::singleShot(500, box, [box, arg] { box->grab().save(arg); box->deleteLater(); });
            }
            std::fprintf(stderr, "[devtools] %s %s\n", qPrintable(cmd), qPrintable(arg));
        });
    }
}
