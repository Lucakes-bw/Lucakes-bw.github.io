#pragma once
#include <QDialog>

class QLineEdit;
class QPushButton;

// 登录对话框（客户端开发专栏《对话框与输入校验》）
class LoginDialog : public QDialog
{
public:
    explicit LoginDialog(QWidget *parent = nullptr);
    QString user() const;
    QString host() const;
    quint16 port() const;

private:
    void updateOk();
    QLineEdit *m_user, *m_server;
    QPushButton *m_ok;
};
