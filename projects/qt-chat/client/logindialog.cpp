#include "logindialog.h"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QVBoxLayout>

LoginDialog::LoginDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("登录 QtChat");
    QSettings settings;                                   // 记住上次的输入
    m_user = new QLineEdit(settings.value("user").toString());
    m_user->setPlaceholderText("字母开头，3～16 位字母、数字或下划线");
    m_user->setValidator(new QRegularExpressionValidator(
        QRegularExpression("[A-Za-z][A-Za-z0-9_]{0,15}"), m_user));
    m_server = new QLineEdit(settings.value("server", "127.0.0.1:9000").toString());

    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->addRow("用户名", m_user);
    form->addRow("服务器", m_server);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_ok = buttons->button(QDialogButtonBox::Ok);
    m_ok->setText("登录");
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        QSettings settings;
        settings.setValue("user", user());
        settings.setValue("server", m_server->text());
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(buttons);
    connect(m_user, &QLineEdit::textChanged, this, &LoginDialog::updateOk);
    connect(m_server, &QLineEdit::textChanged, this, &LoginDialog::updateOk);
    updateOk();
    resize(360, sizeHint().height());
}

QString LoginDialog::user() const { return m_user->text(); }
QString LoginDialog::host() const { return m_server->text().section(':', 0, 0); }
quint16 LoginDialog::port() const { return quint16(m_server->text().section(':', 1, 1).toUInt()); }

void LoginDialog::updateOk()
{
    m_ok->setEnabled(m_user->hasAcceptableInput() && user().size() >= 3 && !host().isEmpty() && port() != 0);
}
