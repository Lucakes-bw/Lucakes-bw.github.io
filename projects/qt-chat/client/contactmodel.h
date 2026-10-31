#pragma once
#include <QAbstractListModel>

// 联系人列表：名字、是否在线、未读数、最后一条消息
class ContactModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles { NameRole = Qt::UserRole, OnlineRole, UnreadRole, LastTextRole };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    void sync(const QList<QPair<QString, bool>> &users);      // 登录后：更新在线状态，保留未读数
    void setOnline(const QString &name, bool online);         // 不认识的名字会被加到末尾
    void setLastText(const QString &name, const QString &text, bool unread);
    void clearUnread(const QString &name);
    int rowOf(const QString &name) const;

private:
    struct Contact {
        QString name;
        bool online = false;
        int unread = 0;
        QString lastText;
    };
    QList<Contact> m_contacts;
};
