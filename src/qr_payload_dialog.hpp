#pragma once
#include <QDialog>
#include <QMap>
#include <expected>

namespace Designer {
enum class QrPayloadType { Text, Url, Wifi, Contact };
std::expected<QString, QString> makeQrPayload(QrPayloadType type, const QMap<QString, QString>& fields);
class QrPayloadDialog final : public QDialog {
    Q_OBJECT
public:
    QrPayloadDialog(const QString& current, QWidget* parent = nullptr);
    QString payload() const;
private:
    QString payload_;
};
}
