#include "connection.h"
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QtEndian>

static constexpr quint32 kMaxFrame = 1 << 20;

Connection::Connection(QObject *parent) : QObject(parent)
{
    m_pingTimer.setInterval(5000);        // 每 5 秒一次心跳
    m_deadTimer.setInterval(15000);       // 15 秒没有收到任何数据就认为断线
    m_deadTimer.setSingleShot(true);
    m_retryTimer.setSingleShot(true);

    connect(&m_socket, &QTcpSocket::connected, this, [this] {
        m_attempt = 0;
        m_buffer.clear();
        m_pingTimer.start();
        m_deadTimer.start();
        emit connected();
    });
    connect(&m_socket, &QTcpSocket::readyRead, this, &Connection::onReadyRead);
    connect(&m_socket, &QTcpSocket::disconnected, this, [this] { onLost("连接被关闭"); });
    connect(&m_socket, &QTcpSocket::errorOccurred, this, [this] {
        if (m_socket.state() != QAbstractSocket::ConnectedState)
            onLost(m_socket.errorString());
    });
    connect(&m_pingTimer, &QTimer::timeout, this, [this] { send({{"type", "ping"}}); });
    connect(&m_deadTimer, &QTimer::timeout, this, [this] {
        onLost("心跳超时");
        m_socket.abort();
    });
    connect(&m_retryTimer, &QTimer::timeout, this, &Connection::connectNow);
}

void Connection::open(const QString &host, quint16 port)
{
    m_host = host;
    m_port = port;
    m_wanted = true;
    m_attempt = 0;
    connectNow();
}

void Connection::close()
{
    m_wanted = false;
    m_retryTimer.stop();
    m_pingTimer.stop();
    m_deadTimer.stop();
    m_socket.abort();
}

void Connection::connectNow()
{
    m_socket.abort();
    m_socket.connectToHost(m_host, m_port);
}

void Connection::send(const QJsonObject &msg)
{
    if (!isConnected())
        return;
    QByteArray body = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    QByteArray header(4, Qt::Uninitialized);
    qToBigEndian<quint32>(quint32(body.size()), header.data());
    m_socket.write(header + body);
}

void Connection::onReadyRead()
{
    m_deadTimer.start();                  // 收到任何数据都说明连接还活着
    m_buffer += m_socket.readAll();
    qsizetype pos = 0;
    while (m_buffer.size() - pos >= 4) {
        quint32 len = qFromBigEndian<quint32>(m_buffer.constData() + pos);
        if (len > kMaxFrame) {
            onLost("收到不合理的消息长度");
            m_socket.abort();
            return;
        }
        if (m_buffer.size() - pos < 4 + qsizetype(len))
            break;
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(m_buffer.mid(pos + 4, len), &err);
        pos += 4 + len;
        if (err.error == QJsonParseError::NoError && doc.isObject())
            emit message(doc.object());
    }
    m_buffer.remove(0, pos);              // 一次性删除已经处理的部分
}

void Connection::onLost(const QString &why)
{
    if (m_retryTimer.isActive())
        return;                           // 已经在等待重连了（一次断线可能收到好几个信号）
    bool wasConnected = m_pingTimer.isActive();
    m_pingTimer.stop();
    m_deadTimer.stop();
    if (wasConnected)
        emit disconnected(why);
    if (!m_wanted)
        return;                           // 是自己调用 close() 断开的，不重连
    // 指数退避：1、2、4、8……最多 30 秒，再加 ±20% 的抖动
    int base = std::min(1000 * (1 << std::min(m_attempt, 5)), 30000);
    int delay = base + int(base * (QRandomGenerator::global()->bounded(0.4) - 0.2));
    ++m_attempt;
    emit retrying((delay + 500) / 1000, why);
    m_retryTimer.start(delay);
}
