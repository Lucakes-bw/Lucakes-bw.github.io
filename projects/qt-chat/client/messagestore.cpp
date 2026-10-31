#include "messagestore.h"
#include <QDateTime>
#include <QDir>
#include <QRandomGenerator>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

static ChatMessage fromQuery(const QSqlQuery &q)
{
    ChatMessage m;
    m.rowId = q.value(0).toLongLong();
    m.peer = q.value(1).toString();
    m.outgoing = q.value(2).toBool();
    m.msgId = q.value(3).toLongLong();
    m.body = q.value(4).toString();
    m.ts = q.value(5).toLongLong();
    m.status = ChatMessage::Status(q.value(6).toInt());
    return m;
}

static const char *kColumns = "id, peer, outgoing, msg_id, body, ts, status";

bool MessageStore::open(const QString &user)
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName(dir + "/" + user + ".db");
    if (!m_db.open())
        return false;
    QSqlQuery q(m_db);
    q.exec("PRAGMA journal_mode=WAL");
    q.finish();                                   // 返回结果的语句用完要 finish
    q.exec("PRAGMA synchronous=NORMAL");
    q.exec("CREATE TABLE IF NOT EXISTS message ("
           " id INTEGER PRIMARY KEY,"
           " peer TEXT NOT NULL,"
           " outgoing INTEGER NOT NULL,"
           " msg_id INTEGER NOT NULL,"
           " body TEXT NOT NULL,"
           " ts INTEGER NOT NULL,"
           " status INTEGER NOT NULL)");
    // 同一个人发来的同一条消息只存一次；按会话和时间查最近的消息
    q.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_msg ON message (peer, outgoing, msg_id)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_peer_ts ON message (peer, ts)");
    return true;
}

ChatMessage MessageStore::addOutgoing(const QString &peer, const QString &body)
{
    ChatMessage m;
    m.peer = peer;
    m.outgoing = true;
    // 随机的 52 位编号：JSON 的数字是双精度浮点数，2^53 以内的整数才能精确表示
    m.msgId = QRandomGenerator::global()->bounded(qint64(1) << 52);
    m.body = body;
    m.ts = QDateTime::currentMSecsSinceEpoch();
    m.status = ChatMessage::Sending;
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO message (peer, outgoing, msg_id, body, ts, status) VALUES (?, 1, ?, ?, ?, ?)");
    q.addBindValue(peer);
    q.addBindValue(m.msgId);
    q.addBindValue(body);
    q.addBindValue(m.ts);
    q.addBindValue(int(m.status));
    q.exec();
    m.rowId = q.lastInsertId().toLongLong();
    return m;
}

bool MessageStore::addIncoming(ChatMessage &m)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT OR IGNORE INTO message (peer, outgoing, msg_id, body, ts, status) VALUES (?, 0, ?, ?, ?, ?)");
    q.addBindValue(m.peer);
    q.addBindValue(m.msgId);
    q.addBindValue(m.body);
    q.addBindValue(m.ts);
    q.addBindValue(int(ChatMessage::Received));
    q.exec();
    if (q.numRowsAffected() == 0)
        return false;                             // 唯一索引冲突：这条消息已经收到过了
    m.rowId = q.lastInsertId().toLongLong();
    m.status = ChatMessage::Received;
    return true;
}

void MessageStore::setStatus(qint64 msgId, ChatMessage::Status status)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE message SET status = ? WHERE outgoing = 1 AND msg_id = ?");
    q.addBindValue(int(status));
    q.addBindValue(msgId);
    q.exec();
}

QList<ChatMessage> MessageStore::recent(const QString &peer, int limit)
{
    QSqlQuery q(m_db);
    q.prepare(QString("SELECT %1 FROM message WHERE peer = ? ORDER BY ts DESC LIMIT ?").arg(kColumns));
    q.addBindValue(peer);
    q.addBindValue(limit);
    q.exec();
    QList<ChatMessage> list;
    while (q.next())
        list.prepend(fromQuery(q));               // 倒序查出最近的，再翻转成时间顺序
    return list;
}

QList<ChatMessage> MessageStore::pending()
{
    QSqlQuery q(m_db);
    q.exec(QString("SELECT %1 FROM message WHERE outgoing = 1 AND status = 0 ORDER BY id").arg(kColumns));
    QList<ChatMessage> list;
    while (q.next())
        list << fromQuery(q);
    return list;
}
