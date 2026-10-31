#pragma once
#include <QMainWindow>
#include "chatclient.h"
#include "contactmodel.h"
#include "messagemodel.h"

class QLabel;
class QListView;
class QPlainTextEdit;
class QPushButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(ChatClient *client, QWidget *parent = nullptr);

    // 给测试脚本用
    void openChat(const QString &peer);
    void setDraft(const QString &text);
    void sendDraft();

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    void onMessageAdded(const ChatMessage &m);
    void scrollToBottomLater();

    ChatClient *m_client;
    ContactModel m_contacts;
    MessageModel m_messages;
    QListView *m_contactView;
    QListView *m_messageView;
    QLabel *m_title;
    QPlainTextEdit *m_input;
    QPushButton *m_send;
    QLabel *m_state;
};
