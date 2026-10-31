#pragma once
#include <QJsonObject>
#include <QTcpSocket>
#include <QTimer>

// 一条到服务器的长连接：分帧、心跳、断线自动重连
// （客户端开发专栏《QTcpSocket 与消息分帧》《断线检测与重连》）
class Connection : public QObject
{
    Q_OBJECT
public:
    explicit Connection(QObject *parent = nullptr);
    ~Connection() override { m_socket.disconnect(this); }

    void open(const QString &host, quint16 port);
    void close();                         // 主动断开，不再重连
    bool isConnected() const { return m_socket.state() == QAbstractSocket::ConnectedState; }
    void send(const QJsonObject &msg);

signals:
    void connected();
    void disconnected(const QString &reason);
    void message(const QJsonObject &msg);
    void retrying(int seconds, const QString &reason);   // 多少秒后重连

private:
    void connectNow();
    void onReadyRead();
    void onLost(const QString &why);

    QString m_host;
    quint16 m_port = 0;
    bool m_wanted = false;                // 是否希望保持连接
    QTcpSocket m_socket;
    QByteArray m_buffer;
    QTimer m_pingTimer;
    QTimer m_deadTimer;
    QTimer m_retryTimer;
    int m_attempt = 0;
};
