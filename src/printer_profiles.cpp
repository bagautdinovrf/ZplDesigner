#include "printer_profiles.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#include <cmath>
#include <algorithm>
#include <limits>

namespace Designer {
using namespace Qt::StringLiterals;
namespace {
constexpr qsizetype maximumCatalogueBytes = 1024 * 1024;
constexpr qsizetype maximumProfiles = 100;

bool validText(const QString& text, qsizetype limit)
{
    return text.size() <= limit && std::ranges::none_of(text, [](QChar ch) { return ch.category() == QChar::Other_Control; });
}

QJsonObject encodeParameters(const PrinterParameters& p)
{
    QJsonObject result{{u"mediaTracking"_s, p.mediaTracking}, {u"printMethod"_s, p.printMethod}, {u"printMode"_s, p.printMode}};
    const auto add = [&](const QString& key, std::optional<int> value) { if (value) result.insert(key, *value); };
    add(u"darkness"_s, p.darkness); add(u"speed"_s, p.speed); add(u"tearOff"_s, p.tearOff);
    add(u"labelTop"_s, p.labelTop); add(u"labelShift"_s, p.labelShift); add(u"maximumLength"_s, p.maximumLength);
    return result;
}

bool integer(const QJsonObject& object, const QString& key, int& value)
{
    const auto json = object.value(key);
    const double number = json.toDouble(std::numeric_limits<double>::quiet_NaN());
    if (!json.isDouble() || !std::isfinite(number) || std::floor(number) != number
        || number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max()) return false;
    value = static_cast<int>(number);
    return true;
}

bool optionalInteger(const QJsonObject& object, const QString& key, std::optional<int>& value)
{
    if (!object.contains(key)) return true;
    int parsed = 0;
    if (!integer(object, key, parsed)) return false;
    value = parsed;
    return true;
}

bool textField(const QJsonObject& object, const QString& key, QString& value)
{
    if (!object.value(key).isString()) return false;
    value = object.value(key).toString();
    return true;
}
} // namespace

QString printerProfilesPath()
{
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return directory.isEmpty() ? QString{} : QDir(directory).filePath(u"printer-profiles.json"_s);
}

QString validatePrinterProfile(const PrinterProfile& p)
{
    if (p.name.trimmed().isEmpty() || !validText(p.name, 100)) return u"Имя профиля: от 1 до 100 символов без управляющих символов."_s;
    if (p.destination.connection != PrintConnection::InstalledPrinter && p.destination.connection != PrintConnection::Tcp)
        return u"Неизвестный способ подключения."_s;
    if (p.mode != LabelPrintMode::Image && p.mode != LabelPrintMode::Zpl && p.mode != LabelPrintMode::Png)
        return u"Неизвестный режим печати."_s;
    if (p.mode == LabelPrintMode::Png && p.destination.connection != PrintConnection::Tcp)
        return u"Отправка PNG доступна только через IP-подключение."_s;
    if (p.dpi != 0 && p.dpi != 203 && p.dpi != 300 && p.dpi != 600) return u"Разрешение профиля: как в документе, 203, 300 или 600 dpi."_s;
    if (!validText(p.destination.host, 253) || !validText(p.destination.printerName, 512)) return u"Некорректное имя принтера или адрес узла."_s;
    if (p.destination.port == 0) return u"Порт должен быть от 1 до 65535."_s;
    if (p.destination.connection == PrintConnection::Tcp && (p.destination.host.trimmed().isEmpty() || p.destination.port == 0))
        return u"Для IP-подключения укажите адрес и порт 1–65535."_s;
    if (p.destination.connection == PrintConnection::InstalledPrinter && p.destination.printerName.trimmed().isEmpty())
        return u"Выберите установленный принтер для профиля."_s;
    if (p.destination.timeoutMs < 1 || p.destination.timeoutMs > 60000) return u"Тайм-аут должен быть от 1 до 60000 мс."_s;
    return validatePrinterParameters(p.parameters);
}

std::expected<QList<PrinterProfile>, QString> loadPrinterProfiles(const QString& requestedPath)
{
    const auto path = requestedPath.isEmpty() ? printerProfilesPath() : requestedPath;
    if (path.isEmpty()) return std::unexpected(u"Каталог профилей принтера недоступен."_s);
    QFile file(path);
    if (!file.exists()) return QList<PrinterProfile>{};
    if (!file.open(QIODevice::ReadOnly)) return std::unexpected(u"Не удалось прочитать профили: %1"_s.arg(file.errorString()));
    if (file.size() > maximumCatalogueBytes) return std::unexpected(u"Файл профилей превышает 1 МиБ."_s);
    QJsonParseError parseError;
    const auto bytes = file.read(maximumCatalogueBytes + 1);
    if (bytes.size() > maximumCatalogueBytes || file.error() != QFileDevice::NoError)
        return std::unexpected(u"Не удалось полностью прочитать файл профилей (максимум 1 МиБ)."_s);
    const auto json = QJsonDocument::fromJson(bytes, &parseError);
    const auto root = json.object();
    int version = 0;
    if (parseError.error != QJsonParseError::NoError || !json.isObject() || !integer(root, u"version"_s, version)
        || version != 1 || !root.value(u"profiles"_s).isArray())
        return std::unexpected(u"Файл профилей повреждён или имеет неподдерживаемую версию."_s);
    const auto array = root.value(u"profiles"_s).toArray();
    if (array.size() > maximumProfiles) return std::unexpected(u"Допустимо не более 100 профилей."_s);
    QList<PrinterProfile> profiles;
    QSet<QString> names;
    for (const auto& item : array) {
        PrinterProfile p;
        const auto object = item.toObject();
        const auto parameters = object.value(u"parameters"_s).toObject();
        int connection = 0, mode = 0, port = 0;
        if (!item.isObject() || !textField(object, u"name"_s, p.name)
            || !textField(object, u"printer"_s, p.destination.printerName) || !textField(object, u"host"_s, p.destination.host)
            || !integer(object, u"connection"_s, connection) || !integer(object, u"mode"_s, mode)
            || !integer(object, u"port"_s, port) || port < 1 || port > 65535
            || !integer(object, u"timeoutMs"_s, p.destination.timeoutMs) || !integer(object, u"dpi"_s, p.dpi)
            || !object.value(u"usePrinterPaper"_s).isBool() || !object.value(u"parameters"_s).isObject()
            || !textField(parameters, u"mediaTracking"_s, p.parameters.mediaTracking)
            || !textField(parameters, u"printMethod"_s, p.parameters.printMethod)
            || !textField(parameters, u"printMode"_s, p.parameters.printMode)
            || !optionalInteger(parameters, u"darkness"_s, p.parameters.darkness)
            || !optionalInteger(parameters, u"speed"_s, p.parameters.speed)
            || !optionalInteger(parameters, u"tearOff"_s, p.parameters.tearOff)
            || !optionalInteger(parameters, u"labelTop"_s, p.parameters.labelTop)
            || !optionalInteger(parameters, u"labelShift"_s, p.parameters.labelShift)
            || !optionalInteger(parameters, u"maximumLength"_s, p.parameters.maximumLength))
            return std::unexpected(u"Некорректные данные профиля принтера."_s);
        p.destination.connection = static_cast<PrintConnection>(connection);
        p.destination.port = static_cast<quint16>(port);
        p.destination.usePrinterPaper = object.value(u"usePrinterPaper"_s).toBool();
        p.mode = static_cast<LabelPrintMode>(mode);
        const auto error = validatePrinterProfile(p);
        if (!error.isEmpty()) return std::unexpected(u"Профиль «%1»: %2"_s.arg(p.name, error));
        const auto key = p.name.trimmed().toCaseFolded();
        if (names.contains(key)) return std::unexpected(u"Имена профилей должны быть уникальны."_s);
        names.insert(key);
        profiles.append(std::move(p));
    }
    return profiles;
}

std::expected<void, QString> savePrinterProfiles(const QList<PrinterProfile>& profiles, const QString& requestedPath)
{
    if (profiles.size() > maximumProfiles) return std::unexpected(u"Допустимо не более 100 профилей."_s);
    QJsonArray array;
    QSet<QString> names;
    for (const auto& p : profiles) {
        const auto error = validatePrinterProfile(p);
        if (!error.isEmpty()) return std::unexpected(error);
        const auto key = p.name.trimmed().toCaseFolded();
        if (names.contains(key)) return std::unexpected(u"Имена профилей должны быть уникальны."_s);
        names.insert(key);
        array.append(QJsonObject{{u"name"_s, p.name.trimmed()}, {u"connection"_s, static_cast<int>(p.destination.connection)},
            {u"printer"_s, p.destination.printerName}, {u"host"_s, p.destination.host}, {u"port"_s, p.destination.port},
            {u"timeoutMs"_s, p.destination.timeoutMs}, {u"usePrinterPaper"_s, p.destination.usePrinterPaper},
            {u"mode"_s, static_cast<int>(p.mode)}, {u"dpi"_s, p.dpi}, {u"parameters"_s, encodeParameters(p.parameters)}});
    }
    const auto bytes = QJsonDocument(QJsonObject{{u"version"_s, 1}, {u"profiles"_s, array}}).toJson();
    const auto path = requestedPath.isEmpty() ? printerProfilesPath() : requestedPath;
    if (path.isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath())) return std::unexpected(u"Не удалось создать каталог профилей."_s);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return std::unexpected(u"Не удалось сохранить профили: %1"_s.arg(file.errorString()));
    return {};
}
} // namespace Designer
