#include "messagemodel.h"
#include <QDateTime>

int MessageModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_messages.size());
}

QVariant MessageModel::data(const QModelIndex &index, int role) const
{
    const ChatMessage &m = m_messages[index.row()];
    switch (role) {
    case Qt::DisplayRole:  return m.body;
    case OutgoingRole:     return m.outgoing;
    case StatusRole:       return int(m.status);
    case TimeRole:         return QDateTime::fromMSecsSinceEpoch(m.ts).toString("HH:mm");
    }
    return {};
}

void MessageModel::load(const QString &peer, const QList<ChatMessage> &messages)
{
    beginResetModel();                     // 切换会话：整个列表都换了
    m_peer = peer;
    m_messages = messages;
    endResetModel();
}

void MessageModel::append(const ChatMessage &m)
{
    int row = int(m_messages.size());
    beginInsertRows({}, row, row);
    m_messages.append(m);
    endInsertRows();
}

void MessageModel::setStatus(qint64 msgId, ChatMessage::Status status)
{
    for (int i = int(m_messages.size()) - 1; i >= 0; --i) {   // 一般是最近的几条，从后往前找
        if (m_messages[i].outgoing && m_messages[i].msgId == msgId) {
            m_messages[i].status = status;
            emit dataChanged(index(i), index(i), {StatusRole});
            return;
        }
    }
}
