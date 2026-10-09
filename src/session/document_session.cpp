#include "document_session.hpp"
#include "eac_mark.hpp"
#include <QSignalBlocker>
#include "data_matrix_values.hpp"
#include "project_codec.hpp"

#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QUndoCommand>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Designer {
namespace {
constexpr qint64 maximumFileBytes = 8 * 1024 * 1024;
constexpr qint64 maximumLayoutBytes = 64 * 1024;

bool validDpi(int dpi)
{
    return dpi == 203 || dpi == 300 || dpi == 600;
}

bool validLayoutSettings(const CanvasLayoutSettings& settings)
{
    const auto validGuides = [](const QList<int>& guides) {
        return guides.size() <= 128
            && std::ranges::all_of(guides, [](int value) { return value >= 0 && value <= 4096; });
    };
    return settings.gridStep >= 1 && settings.gridStep <= 4096
        && settings.margin >= 0 && settings.margin <= 2048
        && (settings.unit == CanvasUnit::Millimeters || settings.unit == CanvasUnit::Dots)
        && validGuides(settings.horizontalGuides) && validGuides(settings.verticalGuides);
}

void canonicalizeGuides(CanvasLayoutSettings& settings)
{
    for (auto* guides : {&settings.horizontalGuides, &settings.verticalGuides}) {
        std::ranges::sort(*guides);
        guides->erase(std::unique(guides->begin(), guides->end()), guides->end());
    }
}

QString sourceHash(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QByteArray layoutBytes(const CanvasLayoutSettings& settings, int dpi, const QByteArray& zpl)
{
    const auto guides = [](const QList<int>& values) {
        QJsonArray array;
        for (const int value : values) array.append(value);
        return array;
    };
    const QJsonObject layout{
        {QStringLiteral("gridVisible"), settings.gridVisible},
        {QStringLiteral("snapEnabled"), settings.snapEnabled},
        {QStringLiteral("rulersVisible"), settings.rulersVisible},
        {QStringLiteral("guidesVisible"), settings.guidesVisible},
        {QStringLiteral("marginsVisible"), settings.marginsVisible},
        {QStringLiteral("gridStep"), settings.gridStep},
        {QStringLiteral("margin"), settings.margin},
        {QStringLiteral("unit"), settings.unit == CanvasUnit::Millimeters ? QStringLiteral("mm") : QStringLiteral("dots")},
        {QStringLiteral("horizontalGuides"), guides(settings.horizontalGuides)},
        {QStringLiteral("verticalGuides"), guides(settings.verticalGuides)}
    };
    return QJsonDocument(QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("sha256"), sourceHash(zpl)},
        {QStringLiteral("dpi"), dpi},
        {QStringLiteral("layout"), layout}
    }).toJson(QJsonDocument::Indented);
}

struct LoadedLayout {
    CanvasLayoutSettings settings;
    int dpi = 203;
    QString warning;
};

LoadedLayout readLayout(const QString& path, const QByteArray& zpl)
{
    const auto failure = [](const QString& reason) {
        return LoadedLayout{{}, 203, QStringLiteral("Настройки холста не восстановлены: %1 Используются настройки по умолчанию.").arg(reason)};
    };
    const auto localPath = DocumentSession::layoutStoragePath(path);
    // Read old portable sidecars until the next successful save migrates the
    // preferences. A local record is authoritative, including its hash check.
    QFile file(!localPath.isEmpty() && QFileInfo::exists(localPath)
        ? localPath : path + QStringLiteral(".layout.json"));
    if (!QFileInfo::exists(file.fileName())) return {};
    if (!file.open(QIODevice::ReadOnly))
        return failure(QStringLiteral("не удалось открыть %1 (%2).").arg(file.fileName(), file.errorString()));
    if (file.size() > maximumLayoutBytes)
        return failure(QStringLiteral("файл настроек превышает 64 КиБ."));
    const QByteArray bytes = file.read(maximumLayoutBytes + 1);
    if (file.error() != QFileDevice::NoError)
        return failure(QStringLiteral("не удалось прочитать файл настроек (%1).").arg(file.errorString()));
    if (bytes.size() > maximumLayoutBytes)
        return failure(QStringLiteral("файл настроек превышает 64 КиБ."));
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !json.isObject())
        return failure(QStringLiteral("повреждён JSON настроек."));
    const auto object = json.object();
    const auto integer = [](const QJsonValue& value, int minimum, int maximum, int& output) {
        if (!value.isDouble()) return false;
        const double number = value.toDouble();
        if (!std::isfinite(number) || number < minimum || number > maximum || std::floor(number) != number)
            return false;
        output = static_cast<int>(number);
        return true;
    };
    int version = 0;
    if (!integer(object.value(QStringLiteral("version")), 1, 1, version))
        return failure(QStringLiteral("неподдерживаемая версия настроек."));
    if (!object.value(QStringLiteral("sha256")).isString()
        || object.value(QStringLiteral("sha256")).toString() != sourceHash(zpl))
        return failure(QStringLiteral("настройки относятся к другому содержимому ZPL."));
    LoadedLayout result;
    if (!integer(object.value(QStringLiteral("dpi")), 203, 600, result.dpi) || !validDpi(result.dpi)
        || !object.value(QStringLiteral("layout")).isObject())
        return failure(QStringLiteral("недопустимый формат настроек."));
    const auto layout = object.value(QStringLiteral("layout")).toObject();
    const auto boolean = [&layout](const QString& key, bool& output) {
        const auto value = layout.value(key);
        if (!value.isBool()) return false;
        output = value.toBool();
        return true;
    };
    auto& settings = result.settings;
    if (!boolean(QStringLiteral("gridVisible"), settings.gridVisible)
        || !boolean(QStringLiteral("snapEnabled"), settings.snapEnabled)
        || !boolean(QStringLiteral("rulersVisible"), settings.rulersVisible)
        || !boolean(QStringLiteral("guidesVisible"), settings.guidesVisible)
        || !boolean(QStringLiteral("marginsVisible"), settings.marginsVisible)
        || !integer(layout.value(QStringLiteral("gridStep")), 1, 4096, settings.gridStep)
        || !integer(layout.value(QStringLiteral("margin")), 0, 2048, settings.margin))
        return failure(QStringLiteral("недопустимые значения настроек."));
    const auto unit = layout.value(QStringLiteral("unit"));
    if (!unit.isString() || (unit.toString() != QStringLiteral("mm") && unit.toString() != QStringLiteral("dots")))
        return failure(QStringLiteral("неподдерживаемая единица измерения."));
    settings.unit = unit.toString() == QStringLiteral("dots") ? CanvasUnit::Dots : CanvasUnit::Millimeters;
    const auto guides = [&integer](const QJsonValue& value, QList<int>& output) {
        if (!value.isArray() || value.toArray().size() > 128) return false;
        for (const auto coordinate : value.toArray()) {
            int position = 0;
            if (!integer(coordinate, 0, 4096, position)) return false;
            output.append(position);
        }
        return true;
    };
    if (!guides(layout.value(QStringLiteral("horizontalGuides")), settings.horizontalGuides)
        || !guides(layout.value(QStringLiteral("verticalGuides")), settings.verticalGuides))
        return failure(QStringLiteral("недопустимые направляющие."));
    canonicalizeGuides(settings);
    return result;
}

std::expected<void, QString> writeZplFile(const DesignDocument& document, const QString& path, bool preservePlaceholders)
{
    if (path.isEmpty()) return std::unexpected(QStringLiteral("Не указан путь для сохранения ZPL."));
    const auto validation = validateForExport(document, preservePlaceholders);
    if (!validation) return std::unexpected(validation.error());
    const QByteArray bytes = toZpl(document, preservePlaceholders).toUtf8();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return std::unexpected(QStringLiteral("Не удалось открыть файл для записи: %1").arg(file.errorString()));
    if (file.write(bytes) != bytes.size()) {
        file.cancelWriting();
        return std::unexpected(QStringLiteral("Не удалось записать файл: %1").arg(file.errorString()));
    }
    if (!file.commit()) return std::unexpected(QStringLiteral("Не удалось сохранить файл: %1").arg(file.errorString()));
    return {};
}

QString absolutePath(const QString& path)
{
    return path.isEmpty() ? QString{} : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

bool projectPath(const QString& path)
{
    return QFileInfo(path).suffix().compare(QStringLiteral("qtzpl"), Qt::CaseInsensitive) == 0;
}

std::expected<QByteArray, QString> readBoundedFile(const QString& path, qint64 maximum)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::unexpected(QStringLiteral("Не удалось открыть файл: %1").arg(file.errorString()));
    if (file.size() > maximum)
        return std::unexpected(QStringLiteral("Файл превышает допустимый размер."));
    auto bytes = file.read(maximum + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > maximum)
        return std::unexpected(QStringLiteral("Не удалось прочитать файл полностью или превышен лимит размера."));
    return bytes;
}

std::expected<void, QString> writeAtomic(const QString& path, const QByteArray& bytes)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return std::unexpected(QStringLiteral("Не удалось сохранить файл: %1").arg(file.errorString()));
    return {};
}

bool recoveryPathAllowed(const QString& path)
{
    const QFileInfo info(path);
    const QString directory = DocumentSession::recoveryStorageDirectory();
    return !directory.isEmpty() && info.absolutePath() == absolutePath(directory)
        && info.fileName().endsWith(QStringLiteral(".qtzpl.recovery")) && !info.isSymLink();
}

struct RecoveryState {
    ProjectState project;
    QString originalPath;
    QDateTime modifiedAt;
};

std::expected<RecoveryState, QString> readRecovery(const QString& path)
{
    const auto bytes = readBoundedFile(path, maximumProjectBytes + 64 * 1024);
    if (!bytes) return std::unexpected(bytes.error());
    const auto json = QJsonDocument::fromJson(*bytes);
    if (!json.isObject()) return std::unexpected(QStringLiteral("Повреждён файл восстановления."));
    const auto object = json.object();
    if (object.value(QStringLiteral("format")).toString() != QStringLiteral("QtZplDesignerRecovery")
        || object.value(QStringLiteral("version")).toDouble(-1) != 1.0
        || !object.value(QStringLiteral("project")).isObject()
        || !object.value(QStringLiteral("originalPath")).isString()
        || object.value(QStringLiteral("originalPath")).toString().size() > 8192)
        return std::unexpected(QStringLiteral("Неподдерживаемый или повреждённый файл восстановления."));
    auto project = decodeProject(QJsonDocument(object.value(QStringLiteral("project")).toObject()).toJson(QJsonDocument::Compact));
    if (!project) return std::unexpected(project.error());
    const auto timestamp = QDateTime::fromString(object.value(QStringLiteral("modifiedAt")).toString(), Qt::ISODateWithMs);
    if (!timestamp.isValid()) return std::unexpected(QStringLiteral("Повреждена дата файла восстановления."));
    return RecoveryState{std::move(*project), object.value(QStringLiteral("originalPath")).toString(), timestamp};
}

QList<int> validIndices(const DesignDocument& document, const QList<int>& indices, bool allowLocked = false)
{
    if (!document.visualEditable) return {};
    QList<int> result;
    for (const int index : indices) {
        if (index >= 0 && index < document.elements.size() && !result.contains(index))
            result.append(index);
    }
    QSet<QUuid> groups;
    for (const int index : result)
        if (!document.elements[index].groupId.isNull()) groups.insert(document.elements[index].groupId);
    for (int index = 0; index < document.elements.size(); ++index)
        if (groups.contains(document.elements[index].groupId) && !result.contains(index)) result.append(index);
    // A locked member protects its whole group; do not split relative geometry.
    if (!allowLocked && std::ranges::any_of(result, [&](int index) { return document.elements[index].locked; }))
        return {};
    std::ranges::sort(result);
    return result;
}

QRectF renderedRect(const DesignDocument& document, int index, const QList<QRect>& bounds)
{
    const auto& element = document.elements[index];
    if (element.type == ElementType::Eac)
        return mapDocumentRect(document, {element.x, element.y, element.width, element.height});
    // Preview clips shapes at the label edge for hit testing. Layout needs their
    // full geometry, so centering an overflowing box really brings it inside.
    if (element.type == ElementType::Box || element.type == ElementType::Line
        || element.type == ElementType::Ellipse || element.type == ElementType::Circle || element.type == ElementType::Diagonal) {
        const int height = element.type == ElementType::Circle ? element.width : element.height;
        const int justification = element.originJustification < 0 ? document.defaultJustification : element.originJustification;
        return mapDocumentRect(document, {element.x - (justification == 1 ? element.width : 0),
            element.y - (element.baselineOrigin ? height : 0), std::max(1, element.width), std::max(1, height)});
    }
    if (index < bounds.size() && !bounds[index].isEmpty())
        return bounds[index];
    return mapDocumentRect(document, {element.x, element.y, std::max(1, element.width), std::max(1, element.height)});
}

QList<QList<int>> selectionUnits(const DesignDocument& document, const QList<int>& selected)
{
    QList<QList<int>> units;
    QMap<QUuid, int> groups;
    for (const int index : selected) {
        const auto group = document.elements[index].groupId;
        if (group.isNull()) units.append(QList<int>{index});
        else if (groups.contains(group)) units[groups.value(group)].append(index);
        else { groups.insert(group, int(units.size())); units.append(QList<int>{index}); }
    }
    return units;
}

QRectF unitBounds(const DesignDocument& document, const QList<int>& indices, const QList<QRect>& bounds)
{
    QRectF result;
    for (const int index : indices) result = result.united(renderedRect(document, index, bounds));
    return result;
}

QPoint constrainedDelta(const DesignDocument& document, const QList<int>& indices, QPoint delta)
{
    int maxX = std::numeric_limits<int>::max();
    int maxY = std::numeric_limits<int>::max();
    for (const int index : indices) {
        const auto& element = document.elements[index];
        maxX = std::min(maxX, std::max(0, document.width - 1) - element.x);
        maxY = std::min(maxY, std::max(0, document.height - 1) - element.y);
    }
    // Keep the entire relative arrangement; clamp one common delta, never each
    // element independently. Existing out-of-label origins may move back in.
    int leftmost = std::numeric_limits<int>::max();
    int topmost = std::numeric_limits<int>::max();
    for (const int index : indices) {
        leftmost = std::min(leftmost, document.elements[index].x);
        topmost = std::min(topmost, document.elements[index].y);
    }
    const int lowX = std::min(0, -leftmost);
    const int lowY = std::min(0, -topmost);
    return {std::clamp(delta.x(), lowX, std::max(0, maxX)),
            std::clamp(delta.y(), lowY, std::max(0, maxY))};
}
} // namespace

class SessionCommand final : public QUndoCommand {
public:
    SessionCommand(DocumentSession& session, DesignDocument before, DesignDocument after,
                   const QString& title)
        : QUndoCommand(title), session_(session), before_(std::move(before)), after_(std::move(after))
    {
    }

    void undo() override { session_.restore(before_); }
    void redo() override { session_.restore(after_); }

private:
    DocumentSession& session_;
    const DesignDocument before_;
    const DesignDocument after_;
};

class LayoutSettingsCommand final : public QUndoCommand {
public:
    LayoutSettingsCommand(DocumentSession& session, CanvasLayoutSettings before,
                          CanvasLayoutSettings after, const QString& title)
        : QUndoCommand(title), session_(session), before_(std::move(before)), after_(std::move(after))
    {
    }

    void undo() override { session_.restoreLayoutSettings(before_); }
    void redo() override { session_.restoreLayoutSettings(after_); }

private:
    DocumentSession& session_;
    const CanvasLayoutSettings before_;
    const CanvasLayoutSettings after_;
};

DocumentSession::DocumentSession(QObject* parent)
    : QObject(parent)
{
    undo_.setUndoLimit(80);
    recoveryDebounce_.setSingleShot(true);
    recoveryDebounce_.setInterval(1500);
    recoveryDeadline_.setSingleShot(true);
    recoveryDeadline_.setInterval(10000);
    const auto writeRecovery = [this] {
        const auto result = flushRecovery();
        if (!result) emit recoveryWarning(result.error());
    };
    connect(&recoveryDebounce_, &QTimer::timeout, this, writeRecovery);
    connect(&recoveryDeadline_, &QTimer::timeout, this, writeRecovery);
    connect(this, &DocumentSession::documentChanged, this, &DocumentSession::scheduleRecovery);
    connect(this, &DocumentSession::layoutSettingsChanged, this, &DocumentSession::scheduleRecovery);
    connect(&undo_, &QUndoStack::indexChanged, this, [this] {
        // QUndoCommand::redo/undo runs before QUndoStack updates its index.
        // Publish only once both the snapshot and the history/clean state agree.
        const bool documentChanged = std::exchange(documentNotificationPending_, false);
        const bool layoutChanged = std::exchange(layoutNotificationPending_, false);
        if (documentChanged) emit this->documentChanged();
        if (layoutChanged) emit layoutSettingsChanged();
    });
    connect(&undo_, &QUndoStack::cleanChanged, this, [this](bool) {
        emit modifiedChanged(modified());
        scheduleRecovery();
    });
}

DocumentSession::~DocumentSession()
{
    // QUndoStack emits cleanChanged while its own destructor clears commands.
    // Our later-declared recovery members have already been destroyed then.
    disconnect(&undo_, nullptr, this, nullptr);
    disconnect(this, nullptr, this, nullptr);
    recoveryDebounce_.stop();
    recoveryDeadline_.stop();
    if (!modified()) discardRecovery();
}

const DesignDocument& DocumentSession::document() const
{
    return document_;
}

QUndoStack* DocumentSession::undoStack()
{
    return &undo_;
}

QString DocumentSession::filePath() const
{
    return filePath_;
}

bool DocumentSession::modified() const
{
    return !undo_.isClean() || pendingSource_ != savedPendingSource_;
}

const CanvasLayoutSettings& DocumentSession::layoutSettings() const
{
    return layoutSettings_;
}

QString DocumentSession::layoutWarning() const
{
    return layoutWarning_;
}

QString DocumentSession::layoutStoragePath(const QString& zplPath)
{
    if (zplPath.isEmpty()) return {};
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (directory.isEmpty()) return {};
    QString identity = QDir::fromNativeSeparators(absolutePath(zplPath));
#ifdef Q_OS_WIN
    identity = identity.toCaseFolded();
#endif
    return QDir(directory).filePath(QStringLiteral("layouts/%1.json").arg(sourceHash(identity.toUtf8())));
}

QString DocumentSession::recoveryStorageDirectory()
{
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return directory.isEmpty() ? QString{} : QDir(directory).filePath(QStringLiteral("recovery"));
}

QList<RecoveryEntry> DocumentSession::availableRecoveries()
{
    QList<RecoveryEntry> result;
    const QString directory = recoveryStorageDirectory();
    if (directory.isEmpty()) return result;
    const QDir recoveryDirectory(directory);
    const auto files = recoveryDirectory.entryInfoList({QStringLiteral("*.qtzpl.recovery")}, QDir::Files, QDir::Time);
    for (const auto& file : files) {
        if (!recoveryPathAllowed(file.absoluteFilePath())) continue;
        QLockFile lock(file.absoluteFilePath() + QStringLiteral(".lock"));
        lock.setStaleLockTime(0);
        if (!lock.tryLock()) continue; // Another open window still owns this draft.
        const auto state = readRecovery(file.absoluteFilePath());
        if (state) result.append({file.absoluteFilePath(), state->originalPath, state->modifiedAt, {}});
        else result.append({file.absoluteFilePath(), {}, file.lastModified(), state.error()});
    }
    return result;
}

std::expected<void, QString> DocumentSession::discardRecovery(const QString& path)
{
    if (!recoveryPathAllowed(path)) return std::unexpected(tr("Недопустимый путь восстановления."));
    QLockFile lock(path + QStringLiteral(".lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock()) return std::unexpected(tr("Этот черновик уже открыт в другом окне."));
    if (QFileInfo::exists(path) && !QFile::remove(path))
        return std::unexpected(tr("Не удалось удалить файл восстановления."));
    return {};
}

void DocumentSession::discardRecovery()
{
    recoveryDebounce_.stop();
    recoveryDeadline_.stop();
    if (!recoveryPath_.isEmpty() && recoveryLock_) QFile::remove(recoveryPath_);
    recoveryLock_.reset();
    recoveryPath_.clear();
}

QString DocumentSession::recoveryFilePath() const
{
    return recoveryPath_;
}

void DocumentSession::scheduleRecovery()
{
    if (!modified()) {
        discardRecovery();
        return;
    }
    recoveryDebounce_.start();
    if (!recoveryDeadline_.isActive()) recoveryDeadline_.start();
}

std::expected<void, QString> DocumentSession::flushRecovery()
{
    recoveryDebounce_.stop();
    recoveryDeadline_.stop();
    if (!modified()) {
        discardRecovery();
        return {};
    }
    const auto project = encodeProject({document_, layoutSettings_, pendingSource_});
    if (!project) return std::unexpected(tr("Автовосстановление: %1").arg(project.error()));
    if (recoveryPath_.isEmpty()) {
        const auto directory = recoveryStorageDirectory();
        if (directory.isEmpty() || !QDir().mkpath(directory))
            return std::unexpected(tr("Не удалось создать каталог автовосстановления."));
        auto path = QDir(directory).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".qtzpl.recovery"));
        auto lock = std::make_unique<QLockFile>(path + QStringLiteral(".lock"));
        lock->setStaleLockTime(0);
        if (!lock->tryLock()) return std::unexpected(tr("Не удалось заблокировать файл автовосстановления."));
        recoveryPath_ = std::move(path);
        recoveryLock_ = std::move(lock);
    }
    const QJsonObject root{{QStringLiteral("format"), QStringLiteral("QtZplDesignerRecovery")},
        {QStringLiteral("version"), 1}, {QStringLiteral("originalPath"), filePath_},
        {QStringLiteral("modifiedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("project"), QJsonDocument::fromJson(*project).object()}};
    const auto written = writeAtomic(recoveryPath_, QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!written) return std::unexpected(tr("Автовосстановление: %1").arg(written.error()));
    return {};
}

std::expected<void, QString> DocumentSession::recover(const QString& path)
{
    if (!recoveryPathAllowed(path)) return std::unexpected(tr("Недопустимый путь восстановления."));
    auto lock = std::make_unique<QLockFile>(path + QStringLiteral(".lock"));
    lock->setStaleLockTime(0);
    if (!lock->tryLock()) return std::unexpected(tr("Этот черновик уже открыт в другом окне."));
    auto recovery = readRecovery(path);
    if (!recovery) return std::unexpected(recovery.error());
    resetState(std::move(recovery->project.document), recovery->originalPath,
        std::move(recovery->project.layout), {}, std::move(recovery->project.pendingSource), true);
    recoveryPath_ = path;
    recoveryLock_ = std::move(lock);
    // A recovered draft never counts as a saved original, even if its text is
    // unchanged. The user explicitly saves or discards it after inspection.
    scheduleRecovery();
    return {};
}

void DocumentSession::setPendingSource(QString source)
{
    auto applied = toZpl(document_);
    applied.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    applied.replace(u'\r', u'\n');
    auto normalized = source;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(u'\r', u'\n');
    if (normalized == applied) {
        clearPendingSource();
        return;
    }
    if (pendingSource_ == source) return;
    const bool before = modified();
    pendingSource_ = std::move(source);
    sourceError_.clear();
    ++sourceRevision_;
    emit pendingSourceChanged();
    if (before != modified()) emit modifiedChanged(modified());
    scheduleRecovery();
}

void DocumentSession::clearPendingSource()
{
    if (!pendingSource_) return;
    const bool before = modified();
    pendingSource_.reset();
    sourceError_.clear();
    ++sourceRevision_;
    emit pendingSourceChanged();
    if (before != modified()) emit modifiedChanged(modified());
    scheduleRecovery();
}

bool DocumentSession::hasPendingSource() const
{
    return pendingSource_.has_value();
}

QString DocumentSession::pendingSource() const
{
    return pendingSource_.value_or(QString{});
}

SourceStatus DocumentSession::sourceStatus() const
{
    return !pendingSource_ ? SourceStatus::Applied
        : sourceError_.isEmpty() ? SourceStatus::Pending : SourceStatus::Rejected;
}

bool DocumentSession::canEditVisually() const
{
    return document_.visualEditable && !pendingSource_;
}

std::expected<bool, QString> DocumentSession::applySource()
{
    return applySource(revision_, sourceRevision_);
}

std::expected<bool, QString> DocumentSession::applySource(DocumentRevision expectedDocument,
                                                        quint64 expectedSource)
{
    if (expectedDocument != revision_ || expectedSource != sourceRevision_)
        return std::unexpected(tr("Исходник или документ изменился после запроса применения."));
    if (!pendingSource_) return false;
    auto imported = fromZpl(*pendingSource_);
    if (!imported) {
        sourceError_ = imported.error();
        emit pendingSourceChanged();
        return std::unexpected(sourceError_);
    }
    // EAC metadata carries the DPI used to enforce its physical minimum.
    // Keep that portable value when applying a complete saved label.
    if (std::ranges::none_of(imported->elements, [](const Element& element) { return element.type == ElementType::Eac; }))
        imported->dpi = document_.dpi;
    // Observers of the synchronous undo command must see the completed transition.
    pendingSource_.reset();
    sourceError_.clear();
    ++sourceRevision_;
    commit(std::move(*imported), tr("Применить ZPL"), true);
    emit pendingSourceChanged();
    emit modifiedChanged(modified());
    scheduleRecovery();
    return true;
}

void DocumentSession::restoreLayoutSettings(CanvasLayoutSettings settings)
{
    layoutSettings_ = std::move(settings);
    layoutNotificationPending_ = true;
}

void DocumentSession::setLayoutSettings(CanvasLayoutSettings settings, const QString& title)
{
    if (!validLayoutSettings(settings)) return;
    canonicalizeGuides(settings);
    if (settings == layoutSettings_) return;
    undo_.push(new LayoutSettingsCommand(*this, layoutSettings_, std::move(settings),
        title.isEmpty() ? tr("Настройки холста") : title));
}

void DocumentSession::setFilePath(QString path)
{
    path = absolutePath(path);
    if (filePath_ == path)
        return;
    filePath_ = std::move(path);
    emit filePathChanged();
}

void DocumentSession::restore(DesignDocument document)
{
    document.previewValues = document_.previewValues;
    document_ = std::move(document);
    ++revision_.document;
    documentNotificationPending_ = true;
}

void DocumentSession::reset(DesignDocument document, QString path)
{
    resetState(std::move(document), std::move(path), {}, {});
}

void DocumentSession::resetState(DesignDocument document, QString path,
                                 CanvasLayoutSettings settings, QString warning,
                                 std::optional<QString> pending, bool recovered)
{
    discardRecovery();
    pendingSource_ = std::move(pending);
    savedPendingSource_ = recovered ? std::nullopt : pendingSource_;
    sourceError_.clear();
    ++sourceRevision_;
    revision_ = {QUuid::createUuid(), 0, 0};
    documentNotificationPending_ = false;
    layoutNotificationPending_ = false;
    document_ = std::move(document);
    layoutSettings_ = std::move(settings);
    layoutWarning_ = std::move(warning);
    {
        const QSignalBlocker blocker(&undo_);
        undo_.clear();
        if (recovered) undo_.resetClean();
        else undo_.setClean();
    }
    setFilePath(std::move(path));
    emit layoutSettingsChanged();
    emit documentChanged();
    emit pendingSourceChanged();
    emit modifiedChanged(modified());
}

void DocumentSession::commit(DesignDocument document, const QString& title, bool preserveOriginal)
{
    if (pendingSource_) return;
    if (!preserveOriginal)
        document.originalZpl.clear();
    // QUndoStack takes ownership and applies redo synchronously.
    undo_.push(new SessionCommand(*this, document_, std::move(document), title));
}

OperationResult DocumentSession::execute(const EditOperation& operation, const QString& title)
{
    if (!canEditVisually())
        return std::unexpected(tr("Примените исходный ZPL перед визуальным редактированием."));
    if (const auto* resize = std::get_if<ResizeElement>(&operation);
        resize && resize->geometryRevision != revision_)
        return std::unexpected(tr("Геометрия превью устарела. Обновите превью перед изменением размера."));
    auto next = document_;
    const auto result = std::visit([&](const auto& request) { return applyOperation(next, request); }, operation);
    if (result && *result) commit(std::move(next), title);
    return result;
}

bool DocumentSession::moveElements(const QList<int>& indices, QPoint delta)
{
    if (!canEditVisually()) return false;
    const auto selected = validIndices(document_, indices);
    if (selected.isEmpty()) return false;
    delta = constrainedDelta(document_, selected, delta);
    if (delta.isNull()) return false;
    auto next = document_;
    for (const int index : selected) {
        next.elements[index].x += delta.x();
        next.elements[index].y += delta.y();
    }
    commit(std::move(next), tr("Переместить элементы"));
    return true;
}

bool DocumentSession::deleteElements(const QList<int>& indices)
{
    if (!canEditVisually()) return false;
    const auto selected = validIndices(document_, indices);
    if (selected.isEmpty()) return false;
    auto next = document_;
    for (auto it = selected.crbegin(); it != selected.crend(); ++it)
        next.elements.removeAt(*it);
    commit(std::move(next), tr("Удалить элементы"));
    return true;
}

QList<int> DocumentSession::duplicateElements(const QList<int>& indices, QPoint offset)
{
    if (!canEditVisually()) return {};
    const auto selected = validIndices(document_, indices, true);
    if (selected.isEmpty() || document_.elements.size() + selected.size() > 200) return {};
    offset = constrainedDelta(document_, selected, offset);
    auto next = document_;
    QList<int> copies;
    QMap<QUuid, QUuid> groups;
    for (const int index : selected) {
        auto element = document_.elements[index];
        element.id = QUuid::createUuid();
        if (!element.groupId.isNull()) {
            if (!groups.contains(element.groupId)) groups.insert(element.groupId, QUuid::createUuid());
            element.groupId = groups.value(element.groupId);
        }
        element.sourcePrefix.clear();
        element.x += offset.x();
        element.y += offset.y();
        copies.append(static_cast<int>(next.elements.size()));
        next.elements.append(std::move(element));
    }
    commit(std::move(next), tr("Дублировать элементы"));
    return copies;
}

QList<int> DocumentSession::pasteElements(const QList<Element>& elements, QPoint offset)
{
    if (!canEditVisually()) return {};
    if (!document_.visualEditable || elements.isEmpty() || document_.elements.size() + elements.size() > 200) return {};
    auto next = document_;
    QMap<QUuid, QUuid> groups;
    QList<int> inserted;
    int minimumX = 4096, maximumX = -4096, minimumY = 4096, maximumY = -4096;
    for (const auto& element : elements) {
        minimumX = std::min(minimumX, element.x); maximumX = std::max(maximumX, element.x);
        minimumY = std::min(minimumY, element.y); maximumY = std::max(maximumY, element.y);
    }
    offset.setX(std::clamp(offset.x(), -4096 - minimumX, 4096 - maximumX));
    offset.setY(std::clamp(offset.y(), -4096 - minimumY, 4096 - maximumY));
    for (auto element : elements) {
        element.id = QUuid::createUuid();
        if (!element.groupId.isNull()) {
            if (!groups.contains(element.groupId)) groups.insert(element.groupId, QUuid::createUuid());
            element.groupId = groups.value(element.groupId);
        }
        element.x += offset.x(); element.y += offset.y();
        if (element.type == ElementType::Eac)
            element.width = element.height = eacSizeForRequest(
                std::max(element.width, element.height), next.dpi, element.eacStyle);
        element.sourcePrefix.clear(); element.sourceZpl.clear();
        element.sourceState.clear(); element.sourceSignature.clear();
        inserted.append(int(next.elements.size()));
        next.elements.append(std::move(element));
    }
    commit(std::move(next), tr("Вставить объекты"));
    return inserted;
}

bool DocumentSession::groupElements(const QList<int>& indices)
{
    if (!canEditVisually()) return false;
    const auto selected = validIndices(document_, indices);
    if (selected.size() < 2) return false;
    auto next = document_;
    const QUuid group = QUuid::createUuid();
    for (const int index : selected) next.elements[index].groupId = group;
    commit(std::move(next), tr("Сгруппировать объекты"));
    return true;
}

bool DocumentSession::ungroupElements(const QList<int>& indices)
{
    if (!canEditVisually()) return false;
    const auto selected = validIndices(document_, indices);
    QSet<QUuid> groups;
    for (const int index : selected)
        if (!document_.elements[index].groupId.isNull()) groups.insert(document_.elements[index].groupId);
    if (groups.isEmpty()) return false;
    auto next = document_;
    for (auto& element : next.elements)
        if (groups.contains(element.groupId)) element.groupId = {};
    commit(std::move(next), tr("Разгруппировать объекты"));
    return true;
}

bool DocumentSession::alignElements(const QList<int>& indices, ElementAlignment alignment,
                                    AlignmentReference reference, const QList<QRect>& renderedBounds)
{
    if (!canEditVisually()) return false;
    const auto selected = validIndices(document_, indices);
    if (selected.isEmpty()) return false;
    const auto units = selectionUnits(document_, selected);
    QRectF target(0, 0, document_.width, document_.height);
    if (reference == AlignmentReference::Selection) {
        if (units.size() < 2) return false;
        target = {};
        for (const int index : selected)
            target = target.united(renderedRect(document_, index, renderedBounds));
    }
    auto next = document_;
    bool changed = false;
    for (const auto& unit : units) {
        const auto bounds = unitBounds(document_, unit, renderedBounds);
        QPoint delta;
        switch (alignment) {
        case ElementAlignment::Left: delta.setX(qRound(target.left() - bounds.left())); break;
        case ElementAlignment::HorizontalCenter: delta.setX(qRound(target.center().x() - bounds.center().x())); break;
        case ElementAlignment::Right: delta.setX(qRound(target.right() - bounds.right())); break;
        case ElementAlignment::Top: delta.setY(qRound(target.top() - bounds.top())); break;
        case ElementAlignment::VerticalCenter: delta.setY(qRound(target.center().y() - bounds.center().y())); break;
        case ElementAlignment::Bottom: delta.setY(qRound(target.bottom() - bounds.bottom())); break;
        }
        delta = mapDocumentDelta(document_, delta);
        for (const int index : unit) {
            auto& element = next.elements[index];
            // Rendered text/barcodes can start after their ZPL origin. Negative
            // origins are valid and necessary to align that visible edge to zero.
            const int x = element.x + delta.x();
            const int y = element.y + delta.y();
            if (x < -4096 || x > 4096 || y < -4096 || y > 4096) return false;
            changed |= x != element.x || y != element.y;
            element.x = x;
            element.y = y;
        }
    }
    if (changed) commit(std::move(next), tr("Выровнять элементы"));
    return changed;
}

bool DocumentSession::distributeElements(const QList<int>& indices, Qt::Orientation orientation,
                                         const QList<QRect>& renderedBounds)
{
    if (!canEditVisually()) return false;
    auto selected = selectionUnits(document_, validIndices(document_, indices));
    if (selected.size() < 3) return false;
    const bool horizontal = orientation == Qt::Horizontal;
    const auto coordinate = [horizontal](const QRectF& rect) { return horizontal ? rect.left() : rect.top(); };
    const auto extent = [horizontal](const QRectF& rect) { return horizontal ? rect.width() : rect.height(); };
    std::stable_sort(selected.begin(), selected.end(), [&](const QList<int>& left, const QList<int>& right) {
        return coordinate(unitBounds(document_, left, renderedBounds))
            < coordinate(unitBounds(document_, right, renderedBounds));
    });
    const auto first = unitBounds(document_, selected.front(), renderedBounds);
    const auto last = unitBounds(document_, selected.back(), renderedBounds);
    qreal totalExtent = 0;
    for (const auto& unit : selected) totalExtent += extent(unitBounds(document_, unit, renderedBounds));
    const qreal gap = (coordinate(last) + extent(last) - coordinate(first) - totalExtent)
        / (selected.size() - 1);
    auto next = document_;
    qreal position = coordinate(first) + extent(first) + gap;
    bool changed = false;
    // The outer elements are anchors; equal gaps include actual rendered sizes.
    for (qsizetype order = 1; order + 1 < selected.size(); ++order) {
        const auto& unit = selected[order];
        const auto bounds = unitBounds(document_, unit, renderedBounds);
        for (const int index : unit) {
            auto& element = next.elements[index];
            int& value = horizontal ? element.x : element.y;
            const int physicalDelta = qRound(position - coordinate(bounds));
            const auto logicalDelta = mapDocumentDelta(document_, horizontal ? QPoint(physicalDelta, 0) : QPoint(0, physicalDelta));
            const int aligned = value + (horizontal ? logicalDelta.x() : logicalDelta.y());
            if (aligned < -4096 || aligned > 4096) return false;
            changed |= aligned != value;
            value = aligned;
        }
        position += extent(bounds) + gap;
    }
    if (changed) commit(std::move(next), tr("Распределить элементы"));
    return changed;
}

std::expected<void, QString> DocumentSession::open(const QString& path)
{
    if (projectPath(path)) {
        const auto bytes = readBoundedFile(path, maximumProjectBytes);
        if (!bytes) return std::unexpected(bytes.error());
        auto project = decodeProject(*bytes);
        if (!project) return std::unexpected(project.error());
        resetState(std::move(project->document), path, std::move(project->layout), {},
            std::move(project->pendingSource));
        return {};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::unexpected(tr("Не удалось открыть файл: %1").arg(file.errorString()));
    if (file.size() > maximumFileBytes)
        return std::unexpected(tr("Файл ZPL превышает лимит 8 МиБ."));

    // Keep the read bounded even if another process extends the file after size().
    const QByteArray bytes = file.read(maximumFileBytes + 1);
    if (file.error() != QFileDevice::NoError)
        return std::unexpected(tr("Не удалось прочитать файл: %1").arg(file.errorString()));
    if (bytes.size() > maximumFileBytes)
        return std::unexpected(tr("Файл ZPL превышает лимит 8 МиБ."));
    QStringDecoder decoder(QStringDecoder::Utf8,
        QStringConverter::Flag::Stateless | QStringConverter::Flag::ConvertInitialBom);
    const QString source = decoder.decode(bytes);
    if (decoder.hasError())
        return std::unexpected(tr("Файл содержит недопустимую последовательность UTF-8."));
    auto parsed = fromZpl(source);
    if (!parsed)
        return std::unexpected(parsed.error());
    auto layout = readLayout(path, bytes);
    if (std::ranges::none_of(parsed->elements, [](const Element& element) { return element.type == ElementType::Eac; }))
        parsed->dpi = layout.dpi;
    resetState(std::move(*parsed), path, std::move(layout.settings), std::move(layout.warning));
    return {};
}

std::expected<void, QString> DocumentSession::save(const QString& path)
{
    if (path.isEmpty()) return std::unexpected(tr("Не указан путь для сохранения."));
    if (projectPath(path)) {
        auto bytes = encodeProject({document_, layoutSettings_, pendingSource_});
        if (!bytes) return std::unexpected(bytes.error());
        const auto written = writeAtomic(path, *bytes);
        if (!written) return written;
        setFilePath(path);
        markSaved();
        return {};
    }
    if (pendingSource_) return std::unexpected(tr("Примените изменения исходного ZPL перед сохранением или сохраните проект .qtzpl."));
    const auto validation = validateForExport(document_);
    if (!validation) return std::unexpected(validation.error());
    if (!validLayoutSettings(layoutSettings_) || !validDpi(document_.dpi))
        return std::unexpected(tr("Недопустимые настройки холста или DPI."));
    const auto metadataPath = layoutStoragePath(path);
    if (metadataPath.isEmpty())
        return std::unexpected(tr("Не удалось определить локальный каталог настроек. ZPL не изменён."));
    if (!QDir().mkpath(QFileInfo(metadataPath).absolutePath()))
        return std::unexpected(tr("Не удалось создать локальный каталог настроек холста: %1. ZPL не изменён.")
            .arg(QFileInfo(metadataPath).absolutePath()));
    if (QFileInfo::exists(metadataPath) && !QFileInfo(metadataPath).isFile())
        return std::unexpected(tr("Не удалось сохранить настройки холста: %1 не является файлом. ZPL не изменён.").arg(metadataPath));
    const QByteArray zpl = toZpl(document_).toUtf8();
    const QByteArray metadata = layoutBytes(layoutSettings_, document_.dpi, zpl);
    QSaveFile file(path);
    QSaveFile layoutFile(metadataPath);
    // Prepare both temporary files before replacing either target. Most metadata
    // failures therefore leave an existing ZPL untouched.
    if (!file.open(QIODevice::WriteOnly))
        return std::unexpected(tr("Не удалось открыть ZPL для записи: %1").arg(file.errorString()));
    if (!layoutFile.open(QIODevice::WriteOnly))
        return std::unexpected(tr("Не удалось открыть настройки холста для записи: %1. ZPL не изменён.").arg(layoutFile.errorString()));
    if (file.write(zpl) != zpl.size())
        return std::unexpected(tr("Не удалось записать ZPL: %1").arg(file.errorString()));
    if (layoutFile.write(metadata) != metadata.size())
        return std::unexpected(tr("Не удалось записать настройки холста: %1. ZPL не изменён.").arg(layoutFile.errorString()));
    if (!file.commit())
        return std::unexpected(tr("Не удалось сохранить ZPL: %1").arg(file.errorString()));
    if (!layoutFile.commit()) {
        // Separate files cannot be committed atomically. Do not claim a clean
        // save when the ZPL changed but its matching editor metadata did not.
        undo_.resetClean();
        return std::unexpected(tr("ZPL сохранён в %1, но настройки холста сохранить не удалось: %2. Повторите сохранение.")
            .arg(path, layoutFile.errorString()));
    }
    setFilePath(path);
    if (!layoutWarning_.isEmpty()) {
        layoutWarning_.clear();
        emit layoutSettingsChanged();
    }
    markSaved();
    return {};
}

void DocumentSession::markSaved()
{
    savedPendingSource_ = pendingSource_;
    undo_.setClean();
    discardRecovery();
    if (!layoutWarning_.isEmpty()) {
        layoutWarning_.clear();
        emit layoutSettingsChanged();
    }
    emit modifiedChanged(false);
}

void DocumentSession::setPreviewValues(QMap<QString, QString> values)
{
    if (document_.previewValues == values) return;
    document_.previewValues = std::move(values);
    ++revision_.values;
    emit documentChanged();
}

std::expected<void, QString> DocumentSession::exportZpl(const QString& path) const
{
    if (pendingSource_) return std::unexpected(tr("Примените изменения исходного ZPL перед экспортом."));
    if (!filePath_.isEmpty() && projectPath(filePath_)
        && absolutePath(path).compare(filePath_, Qt::CaseInsensitive) == 0)
        return std::unexpected(tr("Выберите другой файл, чтобы сохранить исходный проект."));
    return writeZplFile(document_, path, true);
}

std::expected<void, QString> DocumentSession::saveResolved(const QString& path) const
{
    if (pendingSource_)
        return std::unexpected(tr("Примените изменения исходного ZPL перед экспортом."));
    if (!document_.visualEditable)
        return std::unexpected(tr("Заполненный ZPL доступен для визуального шаблона DataMatrix."));
    if (!filePath_.isEmpty() && absolutePath(path).compare(filePath_, Qt::CaseInsensitive) == 0)
        return std::unexpected(tr("Выберите другой файл, чтобы сохранить исходный шаблон."));
    const auto resolved = resolveDataMatrixValues(document_);
    if (!resolved) return std::unexpected(resolved.error());
    return writeZplFile(*resolved, path, false);
}

} // namespace Designer
