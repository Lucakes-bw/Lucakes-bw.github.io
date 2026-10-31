#include "chatclient.h"
#include <QJsonArray>

ChatClient::ChatClient(QObject *parent) : QObject(parent)
{
    connect(&m_conn, &Connection::connected, this, [this] {
        emit stateChanged("正在登录…", false);
        m_conn.send({{"type", "login"}, {"user", m_user}});
    });
    connect(&m_conn, &Connection::disconnected, this, [this](const QString &why) {
        m_loggedIn = false;
        emit stateChanged("连接断开：" + why, false);
    });
    connect(&m_conn, &Connection::retrying, this, [this](int s, const QString &why) {
        emit stateChanged(QString("%1，%2 秒后重连…").arg(why).arg(s), false);
    });
    connect(&m_conn, &Connection::message, this, &ChatClient::onMessage);
}

bool ChatClient::start(const QString &user, const QString &host, quint16 port)
{
    m_user = user;
    if (!m_store.open(user))
        return false;
    emit stateChanged("正在连接…", false);
    m_conn.open(host, port);
    return true;
}

void ChatClient::sendText(const QString &peer, const QString &text)
{
    ChatMessage m = m_store.addOutgoing(peer, text);      // 先存进数据库，状态是「发送中」
    emit messageAdded(m);
    if (m_loggedIn)
        sendFrame(m);                                      // 没有登录时先不发，登录后统一补发
}

void ChatClient::sendFrame(const ChatMessage &m)
{
    m_conn.send({{"type", "msg"}, {"id", m.msgId}, {"to", m.peer}, {"text", m.body}});
}

void ChatClient::onMessage(const QJsonObject &msg)
{
    const QString type = msg["type"].toString();
    if (type == "login_ok") {
        m_loggedIn = true;
        QList<QPair<QString, bool>> users;
        for (const QJsonValue &v : msg["users"].toArray())
            users << qMakePair(v["name"].toString(), v["online"].toBool());
        emit stateChanged("已连接", true);
        emit loggedIn(users);
        // 补发所有还没有被确认的消息：断线前发出、但不知道对方收到没有的，以及断线期间写的
        // 服务器按编号去重，重复发送不会让对方收到两次
        for (const ChatMessage &m : m_store.pending())
            sendFrame(m);
    } else if (type == "ack") {
        qint64 id = qint64(msg["id"].toDouble());
        m_store.setStatus(id, ChatMessage::Sent);
        emit statusChanged(id, ChatMessage::Sent);
    } else if (type == "error") {
        qint64 id = qint64(msg["id"].toDouble());
        m_store.setStatus(id, ChatMessage::Failed);
        emit statusChanged(id, ChatMessage::Failed);
    } else if (type == "msg") {
        ChatMessage m;
        m.peer = msg["from"].toString();
        m.outgoing = false;
        m.msgId = qint64(msg["id"].toDouble());
        m.body = msg["text"].toString();
        m.ts = qint64(msg["ts"].toDouble());
        if (m_store.addIncoming(m))                        // 重复收到的消息不再显示
            emit messageAdded(m);
    } else if (type == "presence") {
        emit presence(msg["user"].toString(), msg["online"].toBool());
    } else if (type == "kicked") {
        m_conn.close();                                    // 不要自动重连，否则两边会互相踢下线
        m_loggedIn = false;
        emit stateChanged("你的账号在别处登录了", false);
        emit kicked();
    }
}
