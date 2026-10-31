#include "contactmodel.h"
#include <QColor>
#include <QPainter>
#include <QPixmap>

// 表示在线状态的小圆点
static QPixmap dot(const QColor &color)
{
    QPixmap pm(20, 20);
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawEllipse(QRectF(2, 2, 6, 6));
    return pm;
}

int ContactModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_contacts.size());
}

QVariant ContactModel::data(const QModelIndex &index, int role) const
{
    const Contact &c = m_contacts[index.row()];
    switch (role) {
    case Qt::DisplayRole:
        return c.unread ? QString("%1（%2）").arg(c.name).arg(c.unread) : c.name;
    case Qt::DecorationRole:
        static const QPixmap on = dot(QColor("#34c759")), off = dot(QColor("#aeaeb2"));
        return c.online ? on : off;
    case Qt::ToolTipRole:
        return c.lastText;
    case Qt::FontRole:
        if (c.unread) {
            QFont f;
            f.setBold(true);
            return f;
        }
        break;
    case NameRole:     return c.name;
    case OnlineRole:   return c.online;
    case UnreadRole:   return c.unread;
    case LastTextRole: return c.lastText;
    }
    return {};
}

void ContactModel::sync(const QList<QPair<QString, bool>> &users)
{
    // 重连后服务器发来完整的列表。不用重置模型，否则选中的联系人、未读数都会丢失
    for (Contact &c : m_contacts)
        c.online = false;
    if (!m_contacts.isEmpty())
        emit dataChanged(index(0), index(int(m_contacts.size()) - 1), {Qt::DecorationRole, OnlineRole});
    for (const auto &[name, online] : users)
        setOnline(name, online);
}

int ContactModel::rowOf(const QString &name) const
{
    for (int i = 0; i < m_contacts.size(); ++i)
        if (m_contacts[i].name == name)
            return i;
    return -1;
}

void ContactModel::setOnline(const QString &name, bool online)
{
    int row = rowOf(name);
    if (row < 0) {
        row = int(m_contacts.size());
        beginInsertRows({}, row, row);     // 新出现的联系人：插入一行
        m_contacts.append({name, online, 0, {}});
        endInsertRows();
        return;
    }
    m_contacts[row].online = online;
    emit dataChanged(index(row), index(row));
}

void ContactModel::setLastText(const QString &name, const QString &text, bool unread)
{
    int row = rowOf(name);
    if (row < 0) {
        setOnline(name, false);
        row = rowOf(name);
    }
    m_contacts[row].lastText = text;
    if (unread)
        ++m_contacts[row].unread;
    emit dataChanged(index(row), index(row));
}

void ContactModel::clearUnread(const QString &name)
{
    int row = rowOf(name);
    if (row >= 0 && m_contacts[row].unread) {
        m_contacts[row].unread = 0;
        emit dataChanged(index(row), index(row));
    }
}
