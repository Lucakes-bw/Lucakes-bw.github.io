#include "bubbledelegate.h"
#include <QPainter>
#include <QPainterPath>
#include "messagemodel.h"

static constexpr int kPadX = 12, kPadY = 8;     // 气泡内边距
static constexpr int kMargin = 12;              // 气泡离视图边缘的距离
static constexpr int kGap = 6;                  // 两条消息之间的距离
static constexpr int kMetaH = 16;               // 时间和状态那一行的高度

// 文字在气泡中占的矩形：最宽是视图宽度的 65%，超过就自动换行
QRect BubbleDelegate::textRect(const QStyleOptionViewItem &option, const QString &text) const
{
    int viewWidth = option.widget ? option.widget->width() : option.rect.width();
    int maxWidth = std::max(80, int(viewWidth * 0.65) - 2 * kPadX);
    return option.fontMetrics.boundingRect(QRect(0, 0, maxWidth, 100000),
                                           Qt::TextWordWrap, text);
}

QSize BubbleDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QRect r = textRect(option, index.data().toString());
    return {r.width() + 2 * kPadX, r.height() + 2 * kPadY + kMetaH + kGap};
}

void BubbleDelegate::paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const QString text = index.data().toString();
    const bool out = index.data(MessageModel::OutgoingRole).toBool();
    const auto status = ChatMessage::Status(index.data(MessageModel::StatusRole).toInt());

    QRect tr = textRect(option, text);
    QRect bubble(0, option.rect.top() + kGap / 2, tr.width() + 2 * kPadX, tr.height() + 2 * kPadY);
    if (out)
        bubble.moveRight(option.rect.right() - kMargin);
    else
        bubble.moveLeft(option.rect.left() + kMargin);

    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(bubble, 12, 12);
    QColor bg = out ? QColor("#0a84ff") : option.palette.color(QPalette::AlternateBase);
    if (out && status == ChatMessage::Sending)
        bg.setAlphaF(0.6);                      // 还没确认的消息颜色浅一些
    p->fillPath(path, bg);

    p->setPen(out ? QColor(Qt::white) : option.palette.color(QPalette::Text));
    p->drawText(bubble.adjusted(kPadX, kPadY, -kPadX, -kPadY), Qt::TextWordWrap, text);

    // 气泡下面一行小字：时间，自己的消息再加上状态
    QString meta = index.data(MessageModel::TimeRole).toString();
    if (out) {
        if (status == ChatMessage::Sending) meta += "  发送中…";
        else if (status == ChatMessage::Failed) meta += "  发送失败";
    }
    QFont small = option.font;
    small.setPointSizeF(small.pointSizeF() * 0.8);
    p->setFont(small);
    p->setPen(status == ChatMessage::Failed && out ? QColor("#ff3b30")
                                                   : option.palette.color(QPalette::PlaceholderText));
    // 自己的消息：和气泡右边对齐；对方的消息：和气泡左边对齐。文字比气泡宽时向外延伸
    int y = bubble.bottom() + 2;
    QRect metaRect = out ? QRect(QPoint(option.rect.left(), y), QPoint(bubble.right(), y + kMetaH))
                         : QRect(QPoint(bubble.left(), y), QPoint(option.rect.right(), y + kMetaH));
    p->drawText(metaRect, (out ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignTop, meta);
    p->restore();
}
