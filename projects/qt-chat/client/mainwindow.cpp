#include "mainwindow.h"
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include "bubbledelegate.h"

MainWindow::MainWindow(ChatClient *client, QWidget *parent) : QMainWindow(parent), m_client(client)
{
    setWindowTitle("QtChat - " + client->user());

    // 左边：联系人
    m_contactView = new QListView;
    m_contactView->setModel(&m_contacts);
    m_contactView->setUniformItemSizes(true);              // 每行一样高（《Model/View》）
    m_contactView->setIconSize(QSize(10, 10));

    // 右边：标题、消息、输入框
    m_title = new QLabel("选择一个联系人开始聊天");
    m_title->setContentsMargins(12, 8, 12, 8);
    QFont tf = m_title->font();
    tf.setBold(true);
    m_title->setFont(tf);

    m_messageView = new QListView;
    m_messageView->setModel(&m_messages);
    m_messageView->setItemDelegate(new BubbleDelegate(m_messageView));
    m_messageView->setSelectionMode(QAbstractItemView::NoSelection);
    m_messageView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_messageView->setResizeMode(QListView::Adjust);        // 宽度变了，重新计算每个气泡的高度
    m_messageView->setFrameShape(QFrame::NoFrame);

    m_input = new QPlainTextEdit;
    m_input->setPlaceholderText("输入消息，回车发送，Shift+回车换行");
    m_input->setMaximumHeight(72);
    m_input->installEventFilter(this);                      // 拦截回车键（《事件的分发与传播》）
    m_send = new QPushButton("发送");
    m_send->setEnabled(false);

    auto *inputRow = new QHBoxLayout;
    inputRow->addWidget(m_input);
    inputRow->addWidget(m_send, 0, Qt::AlignBottom);

    auto *right = new QWidget;
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 8, 8);
    rightLayout->addWidget(m_title);
    rightLayout->addWidget(m_messageView, 1);
    rightLayout->addLayout(inputRow);

    auto *splitter = new QSplitter;
    splitter->addWidget(m_contactView);
    splitter->addWidget(right);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({160, 440});
    setCentralWidget(splitter);

    m_state = new QLabel;
    statusBar()->addWidget(m_state);

    connect(m_contactView->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &cur) {
                if (cur.isValid())
                    openChat(cur.data(ContactModel::NameRole).toString());
            });
    connect(m_send, &QPushButton::clicked, this, &MainWindow::sendDraft);
    connect(m_input, &QPlainTextEdit::textChanged, this, [this] {
        m_send->setEnabled(!m_messages.peer().isEmpty() && !m_input->toPlainText().trimmed().isEmpty());
    });

    connect(client, &ChatClient::stateChanged, this, [this](const QString &text, bool online) {
        m_state->setText((online ? "● " : "○ ") + text);
    });
    connect(client, &ChatClient::loggedIn, &m_contacts, &ContactModel::sync);
    connect(client, &ChatClient::presence, &m_contacts, &ContactModel::setOnline);
    connect(client, &ChatClient::messageAdded, this, &MainWindow::onMessageAdded);
    connect(client, &ChatClient::statusChanged, &m_messages, &MessageModel::setStatus);
    connect(client, &ChatClient::kicked, this, [this] {
        QMessageBox::warning(this, "已下线", "你的账号在另一个地方登录了。");
    });
    resize(640, 480);
}

void MainWindow::openChat(const QString &peer)
{
    m_messages.load(peer, m_client->store().recent(peer));
    m_contacts.clearUnread(peer);
    int row = m_contacts.rowOf(peer);
    if (row >= 0 && m_contactView->currentIndex().row() != row)
        m_contactView->setCurrentIndex(m_contacts.index(row));
    m_title->setText(peer);
    m_input->setFocus();
    scrollToBottomLater();
}

void MainWindow::onMessageAdded(const ChatMessage &m)
{
    bool current = m.peer == m_messages.peer();
    m_contacts.setLastText(m.peer, m.body, !m.outgoing && !current);
    if (!current)
        return;
    QScrollBar *bar = m_messageView->verticalScrollBar();
    bool atBottom = bar->value() >= bar->maximum() - 4;
    m_messages.append(m);
    if (atBottom || m.outgoing)                             // 用户在翻看历史时，不要把他拉到底部
        scrollToBottomLater();
}

void MainWindow::scrollToBottomLater()
{
    // 新的一行要等视图重新布局后才有位置，所以推迟到下一轮事件循环（《跨线程的信号与槽》2.4 节）
    QTimer::singleShot(0, m_messageView, &QListView::scrollToBottom);
}

void MainWindow::setDraft(const QString &text) { m_input->setPlainText(text); }

void MainWindow::sendDraft()
{
    QString text = m_input->toPlainText().trimmed();
    if (text.isEmpty() || m_messages.peer().isEmpty())
        return;
    m_client->sendText(m_messages.peer(), text);
    m_input->clear();
}

bool MainWindow::eventFilter(QObject *obj, QEvent *e)
{
    if (obj == m_input && e->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(e);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
            && !(key->modifiers() & Qt::ShiftModifier)) {
            sendDraft();
            return true;                                    // 拦截：不让输入框插入换行
        }
    }
    return QMainWindow::eventFilter(obj, e);
}
