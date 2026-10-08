#include "qr_payload_dialog.hpp"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace Designer {
namespace {
QString wifiEscape(QString text) {
    text.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    for (const QChar c : QStringLiteral(";,:\"")) text.replace(QString(c), QStringLiteral("\\") + c);
    return text;
}
QString contactEscape(QString text) {
    text.replace(QStringLiteral("\\"), QStringLiteral("\\\\"));
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QStringLiteral("\r"), QStringLiteral("\n"));
    text.replace(QStringLiteral("\n"), QStringLiteral("\\n"));
    text.replace(QStringLiteral(";"), QStringLiteral("\\;"));
    text.replace(QStringLiteral(","), QStringLiteral("\\,"));
    return text;
}
}
std::expected<QString, QString> makeQrPayload(QrPayloadType type, const QMap<QString, QString>& fields) {
    QString result;
    switch (type) {
    case QrPayloadType::Text: result = fields.value(QStringLiteral("text")); break;
    case QrPayloadType::Url: {
        const auto text = fields.value(QStringLiteral("url")).trimmed();
        const QUrl url(text, QUrl::StrictMode);
        if (!url.isValid() || url.host().isEmpty()
            || (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http")))
            return std::unexpected(QStringLiteral("Введите полную ссылку http:// или https://."));
        result = url.toString(QUrl::FullyEncoded); break;
    }
    case QrPayloadType::Wifi: {
        const auto ssid = fields.value(QStringLiteral("ssid"));
        const auto security = fields.value(QStringLiteral("security"), QStringLiteral("WPA"));
        if (ssid.isEmpty()) return std::unexpected(QStringLiteral("Укажите имя сети."));
        if (security != QStringLiteral("WPA") && security != QStringLiteral("WEP") && security != QStringLiteral("nopass"))
            return std::unexpected(QStringLiteral("Неизвестный тип защиты Wi-Fi."));
        const auto password = fields.value(QStringLiteral("password"));
        if (security != QStringLiteral("nopass") && password.isEmpty())
            return std::unexpected(QStringLiteral("Укажите пароль защищённой сети."));
        result = QStringLiteral("WIFI:T:%1;S:%2;%3;").arg(security, wifiEscape(ssid),
            security == QStringLiteral("nopass") ? QString{} : QStringLiteral("P:%1;").arg(wifiEscape(password)));
        break;
    }
    case QrPayloadType::Contact:
        if (fields.value(QStringLiteral("name")).trimmed().isEmpty())
            return std::unexpected(QStringLiteral("Укажите имя контакта."));
        result = QStringLiteral("BEGIN:VCARD\r\nVERSION:3.0\r\nN:;%1;;;\r\nFN:%1\r\n")
            .arg(contactEscape(fields.value(QStringLiteral("name"))));
        for (const auto& pair : {qMakePair(QStringLiteral("phone"), QStringLiteral("TEL")),
                                 qMakePair(QStringLiteral("email"), QStringLiteral("EMAIL")),
                                 qMakePair(QStringLiteral("company"), QStringLiteral("ORG"))}) {
            if (!fields.value(pair.first).isEmpty())
                result += pair.second + u':' + contactEscape(fields.value(pair.first)) + QStringLiteral("\r\n");
        }
        result += QStringLiteral("END:VCARD"); break;
    }
    if (result.isEmpty()) return std::unexpected(QStringLiteral("Содержимое QR не должно быть пустым."));
    if (result.size() > 1024) return std::unexpected(QStringLiteral("Содержимое QR превышает 1024 символа."));
    return result;
}

QrPayloadDialog::QrPayloadDialog(const QString& current, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("qrPayloadDialog")); setWindowTitle(tr("Содержимое QR-кода")); resize(600, 580);
    auto* layout = new QVBoxLayout(this);
    auto* hint = new QLabel(tr("Выберите форму и проверьте итоговую строку. «Применить» заменит содержимое выбранного QR. Для произвольных шаблонов используйте «Текст»."), this);
    hint->setWordWrap(true); layout->addWidget(hint);
    auto* type = new QComboBox(this); type->setObjectName(QStringLiteral("qrPayloadType"));
    type->addItems({tr("Текст"), tr("Ссылка"), tr("Wi-Fi"), tr("Контакт vCard")}); layout->addWidget(type);
    auto* form = new QFormLayout; layout->addLayout(form);
    auto* text = new QPlainTextEdit(current, this); text->setObjectName(QStringLiteral("qrRawText"));
    text->setMaximumHeight(110); form->addRow(tr("Текст"), text);
    QMap<QString, QLineEdit*> controls;
    const auto field = [this, form, &controls](const QString& key, const QString& title) {
        auto* edit = new QLineEdit(this); edit->setObjectName(QStringLiteral("qr_") + key);
        edit->setMaxLength(1024); controls.insert(key, edit); form->addRow(title, edit); return edit;
    };
    field(QStringLiteral("url"), tr("Ссылка")); field(QStringLiteral("ssid"), tr("Имя сети"));
    auto* password = field(QStringLiteral("password"), tr("Пароль")); password->setEchoMode(QLineEdit::Password);
    auto* security = new QComboBox(this); security->setObjectName(QStringLiteral("qrSecurity"));
    security->addItem(QStringLiteral("WPA / WPA2"), QStringLiteral("WPA"));
    security->addItem(QStringLiteral("WEP"), QStringLiteral("WEP"));
    security->addItem(tr("Без пароля"), QStringLiteral("nopass")); form->addRow(tr("Защита"), security);
    field(QStringLiteral("name"), tr("Имя")); field(QStringLiteral("phone"), tr("Телефон"));
    field(QStringLiteral("email"), tr("Email")); field(QStringLiteral("company"), tr("Организация"));
    auto* preview = new QPlainTextEdit(this); preview->setReadOnly(true); preview->setObjectName(QStringLiteral("qrPayloadPreview"));
    layout->addWidget(new QLabel(tr("Итоговое содержимое (включая пароль Wi-Fi)"), this)); layout->addWidget(preview);
    auto* error = new QLabel(this); error->setWordWrap(true); layout->addWidget(error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Применить")); layout->addWidget(buttons);
    const auto update = [this, type, form, text, controls, security, preview, error, buttons] {
        const auto selected = static_cast<QrPayloadType>(type->currentIndex());
        form->setRowVisible(text, selected == QrPayloadType::Text);
        form->setRowVisible(security, selected == QrPayloadType::Wifi);
        QMap<QString, QString> values;
        values.insert(QStringLiteral("text"), text->toPlainText());
        values.insert(QStringLiteral("security"), security->currentData().toString());
        for (auto it = controls.cbegin(); it != controls.cend(); ++it) {
            const bool show = (selected == QrPayloadType::Url && it.key() == QStringLiteral("url"))
                || (selected == QrPayloadType::Wifi && (it.key() == QStringLiteral("ssid") || it.key() == QStringLiteral("password")))
                || (selected == QrPayloadType::Contact && (it.key() == QStringLiteral("name") || it.key() == QStringLiteral("phone")
                    || it.key() == QStringLiteral("email") || it.key() == QStringLiteral("company")));
            form->setRowVisible(it.value(), show); values.insert(it.key(), it.value()->text());
        }
        controls.value(QStringLiteral("password"))->setEnabled(security->currentData().toString() != QStringLiteral("nopass"));
        const auto result = makeQrPayload(selected, values);
        payload_ = result ? *result : QString{};
        preview->setPlainText(payload_); error->setText(result ? QString{} : result.error());
        buttons->button(QDialogButtonBox::Ok)->setEnabled(bool(result));
    };
    connect(type, &QComboBox::currentIndexChanged, this, update);
    connect(security, &QComboBox::currentIndexChanged, this, update);
    connect(text, &QPlainTextEdit::textChanged, this, update);
    for (auto* control : controls) connect(control, &QLineEdit::textChanged, this, update);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    update();
}
QString QrPayloadDialog::payload() const { return payload_; }
}
