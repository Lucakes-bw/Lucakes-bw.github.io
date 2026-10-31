#pragma once
#include <QList>
#include <QSqlDatabase>
#include <QString>

// 一条聊天消息
struct ChatMessage {
    enum Status { Sending = 0, Sent = 1, Failed = 2, Received = 3 };
    qint64 rowId = 0;          // 本地数据库中的编号
    QString peer;              // 会话的对方
    bool outgoing = false;     // 是不是自己发的
    qint64 msgId = 0;          // 协议中的消息编号，服务器按它去重
    QString body;
    qint64 ts = 0;             // 时间（毫秒）
    Status status = Sending;
};

// 本地的消息存储（SQLite），每个用户一个数据库文件
// （客户端开发专栏《SQLite 本地存储》）
class MessageStore
{
public:
    bool open(const QString &user);
    ChatMessage addOutgoing(const QString &peer, const QString &body);
    bool addIncoming(ChatMessage &m);             // 已经存过（重复收到）时返回 false
    void setStatus(qint64 msgId, ChatMessage::Status status);
    QList<ChatMessage> recent(const QString &peer, int limit = 200);
    QList<ChatMessage> pending();                 // 还没有被确认的消息，按发送顺序
    QString path() const { return m_db.databaseName(); }

private:
    QSqlDatabase m_db;
};
