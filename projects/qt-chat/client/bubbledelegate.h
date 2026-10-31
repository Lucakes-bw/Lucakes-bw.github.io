#pragma once
#include <QStyledItemDelegate>

// 把一条消息画成聊天气泡：自己的在右边、蓝色；对方的在左边、灰色
class BubbleDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    QRect textRect(const QStyleOptionViewItem &option, const QString &text) const;
};
