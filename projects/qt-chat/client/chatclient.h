#pragma once
#include "connection.h"
#include "messagestore.h"

// 聊天协议：登录、收发消息、确认与重发
class ChatClient : public QObject
{
    Q_OBJECT
public:
    explicit ChatClient(QObject *parent = nullptr);
    bool start(const QString &user, const QString &host, quint16 port);
    QString user() const { return m_user; }
    MessageStore &store() { return m_store; }
    void sendText(const QString &peer, const QString &text);

signals:
    void stateChanged(const QString &text, bool online);
    void loggedIn(const QList<QPair<QString, bool>> &users);
    void presence(const QString &user, bool online);
    void messageAdded(const ChatMessage &m);              // 新发出或者新收到的消息
    void statusChanged(qint64 msgId, ChatMessage::Status status);
    void kicked();

private:
    void onMessage(const QJsonObject &msg);
    void sendFrame(const ChatMessage &m);

    QString m_user;
    Connection m_conn;
    MessageStore m_store;
    bool m_loggedIn = false;
};
