#pragma once
#include <QAbstractListModel>
#include "messagestore.h"

// 当前会话的消息列表
class MessageModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles { OutgoingRole = Qt::UserRole, StatusRole, TimeRole };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    void load(const QString &peer, const QList<ChatMessage> &messages);
    QString peer() const { return m_peer; }
    void append(const ChatMessage &m);
    void setStatus(qint64 msgId, ChatMessage::Status status);

private:
    QString m_peer;
    QList<ChatMessage> m_messages;
};
