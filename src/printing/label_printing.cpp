#include "label_printing.hpp"
#include "data_matrix_values.hpp"
#include "preview_pages.hpp"

#include <QBuffer>
#include <QDeadlineTimer>
#include <QNetworkProxy>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPrinter>
#include <QPrinterInfo>
#include <QTcpSocket>
#include <QThread>
#include <QMap>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Winspool.h>
#endif

namespace Designer {
using namespace Qt::StringLiterals;
namespace {
constexpr qint64 maximumJobBytes = 64 * 1024 * 1024;
constexpr qsizetype writeChunkBytes = 64 * 1024;

// This is a byte-preserving print adapter, not another ZPL parser/renderer.
// It only changes explicitly selected device options. Field data is opaque;
// ^FX ends at the next command prefix, as specified by Zebra. Ambiguous syntax
// is refused instead of guessing at command boundaries.
std::expected<QByteArray, QString> applyPrinterParameters(const QByteArray& source, const PrinterParameters& p)
{
    if (p.isEmpty()) return source;
    QMap<QByteArray, QByteArray> replacements;
    const auto integer = [&](const QByteArray& opcode, std::optional<int> value) {
        if (value) replacements.insert(opcode, QByteArray::number(*value));
    };
    integer("~SD", p.darkness); integer("^PR", p.speed); integer("~TA", p.tearOff);
    integer("^LT", p.labelTop); integer("^LS", p.labelShift); integer("^ML", p.maximumLength);
    if (p.darkness) replacements.insert("^MD", "0"); // neutralize a source's relative darkness offset
    if (!p.mediaTracking.isEmpty()) replacements.insert("^MN", p.mediaTracking.toLatin1());
    if (!p.printMethod.isEmpty()) replacements.insert("^MT", p.printMethod.toLatin1());
    if (!p.printMode.isEmpty()) replacements.insert("^MM", p.printMode.toLatin1());
    QByteArray commands;
    for (auto it = replacements.cbegin(); it != replacements.cend(); ++it) commands += it.key() + it.value();
    const auto ambiguous = [] { return std::unexpected(u"Параметры профиля нельзя безопасно применить к этому ZPL "
        u"(изменённые разделители, сохранённый формат, двоичная графика или незакрытое поле). "
        u"Выберите «Принтер / исходный ZPL» для всех параметров либо подготовьте обычный формат ^XA…^XZ."_s); };
    QByteArray result;
    result.reserve(source.size() + commands.size());
    qsizetype position = 0;
    int formats = 0;
    bool inFormat = false;
    while (position < source.size()) {
        if (source[position] != '^' && source[position] != '~') { result += source[position++]; continue; }
        if (position + 3 > source.size()) { result += source.mid(position); break; }
        const auto opcode = source.mid(position, 3).toUpper();
        if (opcode == "^CC" || opcode == "~CC" || opcode == "^CT" || opcode == "~CT"
            || opcode == "^CD" || opcode == "~CD" || opcode == "^DF" || opcode == "^XF") return ambiguous();
        if (opcode == "^FD" || opcode == "^FV") {
            auto end = position + 3;
            while (end + 3 <= source.size() && source.mid(end, 3).toUpper() != "^FS") ++end;
            if (end + 3 > source.size()) return ambiguous();
            result += source.mid(position, end + 3 - position);
            position = end + 3;
            continue;
        }
        if (opcode == "^GF" && position + 3 < source.size()
            && source.mid(position + 3, 1).toUpper() != "A") return ambiguous();
        if (opcode == "^XA") {
            if (inFormat) return ambiguous();
            inFormat = true;
            ++formats;
            result += source.mid(position, 3) + commands;
            position += 3;
            continue;
        }
        if (opcode == "^XZ") inFormat = false;
        auto end = position + 3;
        // A literal tilde is allowed as the seventh ^BX parameter.
        int commas = 0;
        while (end < source.size() && source[end] != '^') {
            if (source[end] == '~' && !(opcode == "^BX" && commas == 6 && source[end - 1] == ',')) break;
            if (source[end] == ',') ++commas;
            ++end;
        }
        const auto replacement = replacements.constFind(opcode);
        if (replacement == replacements.cend()) result += source.mid(position, end - position);
        else {
            auto parameterEnd = end;
            while (parameterEnd > position + 3 && (source[parameterEnd - 1] == '\r' || source[parameterEnd - 1] == '\n'
                   || source[parameterEnd - 1] == ' ' || source[parameterEnd - 1] == '\t')) --parameterEnd;
            // Keep secondary arguments (such as slew/backfeed rate or pre-peel).
            const auto comma = source.indexOf(',', position + 3);
            result += source.mid(position, 3) + *replacement;
            if (comma >= 0 && comma < parameterEnd) result += source.mid(comma, parameterEnd - comma);
            result += source.mid(parameterEnd, end - parameterEnd);
        }
        position = end;
    }
    if (formats == 0 || inFormat) return ambiguous();
    return result;
}

QString validateJob(const PreparedLabelPrint& job)
{
    if (job.mode != LabelPrintMode::Image && job.mode != LabelPrintMode::Zpl && job.mode != LabelPrintMode::Png)
        return u"Неизвестный способ печати."_s;
    const auto parametersError = validatePrinterParameters(job.printerParameters);
    if (!parametersError.isEmpty()) return parametersError;
    if (job.repeats < 1 || job.repeats > 100)
        return u"Допустимо от 1 до 100 повторов задания."_s;
    if (job.dpi != 203 && job.dpi != 300 && job.dpi != 600)
        return u"Для печати выберите 203, 300 или 600 dpi в формате этикетки."_s;
    if (job.pageDots.width() < 1 || job.pageDots.height() < 1
        || job.pageDots.width() > 4096 || job.pageDots.height() > 4096)
        return u"Размер печатной этикетки должен быть от 1 до 4096 точек."_s;
    if (job.mode != LabelPrintMode::Zpl && (job.image.isNull() || job.image.size() != job.pageDots))
        return u"Изображение для печати отсутствует или не соответствует размеру этикетки."_s;
    if (job.mode == LabelPrintMode::Zpl && job.zpl.isEmpty())
        return u"Задание ZPL пусто."_s;
    return {};
}

std::expected<PrintSubmission, QString> submitTcp(const QByteArray& bytes, int repeats, bool separateConnections,
                                                const PrintDestination& destination)
{
    if (destination.host.trimmed().isEmpty() || destination.port == 0)
        return std::unexpected(u"Укажите адрес принтера и порт 1–65535."_s);
    if (destination.timeoutMs < 1 || destination.timeoutMs > 60000)
        return std::unexpected(u"Тайм-аут отправки должен быть от 1 до 60000 мс."_s);
    QDeadlineTimer deadline(destination.timeoutMs);
    const auto remaining = [&] { return static_cast<int>(std::max<qint64>(0, deadline.remainingTime())); };
    QTcpSocket socket;
    socket.setProxy(QNetworkProxy::NoProxy);
    qint64 queued = 0;
    const auto failure = [&]() -> std::expected<PrintSubmission, QString> {
        const qint64 transferred = std::max<qint64>(0, queued - socket.bytesToWrite());
        const QString reason = deadline.hasExpired() ? u"истёк тайм-аут"_s : socket.errorString();
        socket.abort();
        return std::unexpected(u"Отправка в %1:%2 прервана: %3. Передано в TCP: %4 байт; "
            u"задание могло быть принято частично. Проверьте принтер перед повтором."_s
            .arg(destination.host).arg(destination.port).arg(reason).arg(transferred));
    };
    for (int repeat = 0; repeat < repeats; ++repeat) {
        if (deadline.hasExpired() || QThread::currentThread()->isInterruptionRequested()) return failure();
        if (repeat == 0 || separateConnections) {
            socket.connectToHost(destination.host.trimmed(), destination.port, QIODevice::WriteOnly);
            if (!socket.waitForConnected(remaining())) {
                if (queued > 0) return failure();
                return std::unexpected(u"Не удалось подключиться к %1:%2: %3"_s
                    .arg(destination.host).arg(destination.port).arg(socket.errorString()));
            }
        }
        qsizetype offset = 0;
        while (offset < bytes.size()) {
            if (deadline.hasExpired() || QThread::currentThread()->isInterruptionRequested()
                || socket.state() != QAbstractSocket::ConnectedState) return failure();
            const auto amount = std::min(writeChunkBytes, bytes.size() - offset);
            const auto accepted = socket.write(bytes.constData() + offset, amount);
            if (accepted <= 0) return failure();
            offset += accepted;
            queued += accepted;
            while (socket.bytesToWrite() > 0) {
                if (deadline.hasExpired() || !socket.waitForBytesWritten(remaining())) return failure();
            }
        }
        if (separateConnections) {
            // Closing frames one complete PNG; concatenated PNG files are not
            // a portable repeat protocol for a raw TCP receiver.
            socket.disconnectFromHost();
            if (socket.state() != QAbstractSocket::UnconnectedState && !socket.waitForDisconnected(remaining()))
                return failure();
        }
    }
    if (!separateConnections) socket.disconnectFromHost();
    return PrintSubmission{queued, 0,
        u"Передано в %1:%2: %3 байт. TCP подтверждает передачу данных; фактическую печать проверьте на принтере."_s
            .arg(destination.host).arg(destination.port).arg(queued)};
}

#ifdef Q_OS_WIN
QString windowsError(DWORD code)
{
    wchar_t* buffer = nullptr;
    const DWORD count = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
        | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    const QString text = count ? QString::fromWCharArray(buffer, static_cast<qsizetype>(count)).trimmed()
                               : u"Код Windows %1"_s.arg(code);
    if (buffer) LocalFree(buffer);
    return text;
}

struct SpoolHandle {
    HANDLE handle = nullptr;
    bool active = false;
    ~SpoolHandle()
    {
        if (!handle) return;
        if (active) AbortPrinter(handle);
        ClosePrinter(handle);
    }
};

std::expected<PrintSubmission, QString> submitRawWindows(const QByteArray& bytes, int repeats,
                                                       const QString& printerName)
{
    if (printerName.trimmed().isEmpty()) return std::unexpected(u"Выберите установленный принтер."_s);
    auto name = printerName.toStdWString();
    SpoolHandle spool;
    if (!OpenPrinterW(name.data(), &spool.handle, nullptr))
        return std::unexpected(u"Не удалось открыть принтер: %1"_s.arg(windowsError(GetLastError())));
    std::wstring title = L"QtZpl Designer";
    std::wstring dataType = L"RAW";
    DOC_INFO_1W info{title.data(), nullptr, dataType.data()};
    const DWORD id = StartDocPrinterW(spool.handle, 1, reinterpret_cast<LPBYTE>(&info));
    if (!id) return std::unexpected(u"Не удалось создать задание RAW: %1"_s.arg(windowsError(GetLastError())));
    spool.active = true;
    qint64 accepted = 0;
    const auto failure = [&](const QString& operation) -> std::expected<PrintSubmission, QString> {
        const auto error = windowsError(GetLastError());
        return std::unexpected(u"%1: %2. Очередь приняла %3 байт; задание №%4 отменяется. "
            u"Проверьте принтер перед повтором."_s.arg(operation, error).arg(accepted).arg(id));
    };
    if (!StartPagePrinter(spool.handle)) return failure(u"Не удалось начать отправку"_s);
    for (int repeat = 0; repeat < repeats; ++repeat) {
        qsizetype offset = 0;
        while (offset < bytes.size()) {
            const DWORD amount = static_cast<DWORD>(std::min(writeChunkBytes, bytes.size() - offset));
            DWORD written = 0;
            if (!WritePrinter(spool.handle, const_cast<char*>(bytes.constData() + offset), amount, &written))
                return failure(u"Не удалось передать ZPL"_s);
            if (written == 0 || written > amount) {
                SetLastError(ERROR_WRITE_FAULT);
                return failure(u"Очередь не приняла данные ZPL"_s);
            }
            offset += written;
            accepted += written;
        }
    }
    if (!EndPagePrinter(spool.handle)) return failure(u"Не удалось завершить отправку"_s);
    if (!EndDocPrinter(spool.handle)) return failure(u"Не удалось завершить задание"_s);
    spool.active = false;
    return PrintSubmission{accepted, id, u"Задание №%1 передано в очередь «%2»: %3 байт."_s.arg(id).arg(printerName).arg(accepted)};
}
#endif
} // namespace

bool PrinterParameters::isEmpty() const
{
    return !darkness && !speed && mediaTracking.isEmpty() && printMethod.isEmpty() && printMode.isEmpty()
        && !tearOff && !labelTop && !labelShift && !maximumLength;
}

QString validatePrinterParameters(const PrinterParameters& p)
{
    const auto outside = [](std::optional<int> value, int minimum, int maximum) { return value && (*value < minimum || *value > maximum); };
    if (outside(p.darkness, 0, 30)) return u"Насыщенность: 0–30."_s;
    if (outside(p.speed, 1, 14)) return u"Скорость: 1–14 дюймов/с; поддержка зависит от модели принтера."_s;
    if (outside(p.tearOff, -120, 120)) return u"Смещение отрыва: −120…120 dots."_s;
    if (outside(p.labelTop, -120, 120)) return u"Смещение по вертикали: −120…120 dots."_s;
    if (outside(p.labelShift, -9999, 9999)) return u"Смещение по горизонтали: −9999…9999 dots."_s;
    if (outside(p.maximumLength, 1, 32000)) return u"Максимальная длина носителя: 1–32000 dots."_s;
    if (!p.mediaTracking.isEmpty() && (p.mediaTracking.size() != 1 || !QStringView{u"NYMWA"}.contains(p.mediaTracking.front())))
        return u"Неизвестный тип носителя."_s;
    if (!p.printMethod.isEmpty() && p.printMethod != u"T" && p.printMethod != u"D") return u"Неизвестный метод печати."_s;
    if (!p.printMode.isEmpty() && (p.printMode.size() != 1 || !QStringView{u"TPRCA"}.contains(p.printMode.front())))
        return u"Неизвестный режим выдачи этикетки."_s;
    return {};
}

std::expected<PreparedLabelPrint, QString> configureLabelPrint(PreparedLabelPrint job, int dpi, const PrinterParameters& parameters)
{
    if (dpi != 0) job.dpi = dpi;
    job.printerParameters = parameters;
    const auto error = validateJob(job);
    if (!error.isEmpty()) return std::unexpected(error);
    return job;
}

std::expected<PreparedLabelPrint, QString> prepareLabelPrint(const DesignDocument& document,
                                                          LabelPrintMode mode, int repeats)
{
    PreparedLabelPrint job;
    job.mode = mode;
    job.dpi = document.dpi;
    job.repeats = repeats;
    job.pageDots = QSize(document.width, document.height);
    if (mode != LabelPrintMode::Image && mode != LabelPrintMode::Zpl && mode != LabelPrintMode::Png)
        return std::unexpected(u"Неизвестный способ печати."_s);
    if (repeats < 1 || repeats > 100) return std::unexpected(u"Допустимо от 1 до 100 повторов задания."_s);
    if (job.dpi != 203 && job.dpi != 300 && job.dpi != 600)
        return std::unexpected(u"Для печати выберите 203, 300 или 600 dpi в формате этикетки."_s);

    auto resolved = resolveDataMatrixValues(document);
    if (!resolved) return std::unexpected(resolved.error() + u" Заполните «Значения плейсхолдеров…» перед печатью."_s);
    const auto valid = validateForExport(*resolved, !resolved->visualEditable);
    if (!valid) return std::unexpected(valid.error());
    const QString source = toZpl(*resolved, false);
    const auto labels = sourceLabels(source);
    if (!labels || labels->labels == 0)
        return std::unexpected(u"В задании нет этикетки ^XA…^XZ."_s);
    if (!resolved->visualEditable && labels->placeholderFields)
        return std::unexpected(u"Исходный ZPL содержит переменные, которые нельзя безопасно подставить. "
            u"Заполните их во вкладке ZPL перед печатью."_s);
    job.labelsPerRepeat = labels->labelsPerRepeat;
    if (mode != LabelPrintMode::Zpl) {
        if (labels->labels != 1)
            return std::unexpected(u"Печать изображением доступна для одной этикетки. "
                u"Для файла с несколькими этикетками выберите ZPL."_s);
        const auto preview = renderPreview(*resolved);
        if (!preview) return std::unexpected(preview.error());
        if (!preview->elementErrors.isEmpty()) return std::unexpected(u"Исправьте ошибочные элементы перед печатью."_s);
        for (const auto& diagnostic : preview->diagnostics)
            if (diagnostic.startsWith(u"unsupported-command:") || diagnostic.startsWith(u"barcode-render-pending:"))
                return std::unexpected(u"QtZpl не может полностью отрисовать эту этикетку: %1 "
                    u"Для принтера, который поддерживает исходные команды, выберите печать ZPL."_s.arg(diagnostic));
        job.image = preview->image.convertToFormat(QImage::Format_Grayscale8);
        job.image.setDevicePixelRatio(1.0);
        job.pageDots = job.image.size();
        job.labelsPerRepeat = 1;
    } else job.zpl = source.toUtf8();
    const auto error = validateJob(job);
    if (!error.isEmpty()) return std::unexpected(error);
    return job;
}

QSizeF printPageMillimeters(const PreparedLabelPrint& job)
{
    if (job.dpi <= 0) return {};
    return QSizeF(job.pageDots.width() * 25.4 / job.dpi, job.pageDots.height() * 25.4 / job.dpi);
}

QRectF printImageTargetRect(const PreparedLabelPrint& job, int deviceDpiX, int deviceDpiY)
{
    if (job.dpi <= 0 || deviceDpiX <= 0 || deviceDpiY <= 0) return {};
    return QRectF(0, 0, qreal(job.pageDots.width()) * deviceDpiX / job.dpi,
                  qreal(job.pageDots.height()) * deviceDpiY / job.dpi);
}

std::expected<QByteArray, QString> printWireBytes(const PreparedLabelPrint& job)
{
    const auto error = validateJob(job);
    if (!error.isEmpty()) return std::unexpected(error);
    QByteArray bytes = job.zpl;
    if (job.mode == LabelPrintMode::Png) {
        auto image = job.image;
        const int dotsPerMeter = qRound(job.dpi / 0.0254);
        image.setDotsPerMeterX(dotsPerMeter);
        image.setDotsPerMeterY(dotsPerMeter);
        bytes.clear();
        QBuffer buffer(&bytes);
        if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
            return std::unexpected(u"Не удалось подготовить PNG для отправки."_s);
    } else if (job.mode == LabelPrintMode::Image) {
        DesignDocument raster;
        raster.width = job.pageDots.width();
        raster.height = job.pageDots.height();
        raster.dpi = job.dpi;
        auto element = makeElement(ElementType::Image, {0, 0});
        element.width = raster.width;
        element.height = raster.height;
        element.image = job.image;
        raster.elements.append(std::move(element));
        bytes = toZpl(raster, false).toUtf8();
    }
    if (job.mode != LabelPrintMode::Png) {
        const auto configured = applyPrinterParameters(bytes, job.printerParameters);
        if (!configured) return std::unexpected(configured.error());
        bytes = *configured;
    }
    if (bytes.isEmpty() || qint64(bytes.size()) * job.repeats > maximumJobBytes)
        return std::unexpected(u"Общий объём задания превышает 64 МиБ. Уменьшите число повторов."_s);
    return bytes;
}

std::expected<PrintSubmission, QString> paintLabelPrint(const PreparedLabelPrint& job, QPrinter& printer,
                                                      bool usePrinterPaper)
{
    const auto error = validateJob(job);
    if (!error.isEmpty()) return std::unexpected(error);
    if (job.mode != LabelPrintMode::Image) return std::unexpected(u"Для печати через драйвер требуется изображение."_s);
    if (!printer.isValid()) return std::unexpected(u"Выбранный принтер недоступен."_s);
    printer.setDocName(u"QtZpl Designer"_s);
    printer.setResolution(job.dpi);
    printer.setColorMode(QPrinter::GrayScale);
    printer.setCopyCount(1); // Repeat pages ourselves, including drivers with no copy support.
    printer.setDuplex(QPrinter::DuplexNone);
    const auto expected = printPageMillimeters(job);
    if (usePrinterPaper) {
        printer.setFullPage(false);
        const auto printable = printer.pageRect(QPrinter::Millimeter).size();
        if (expected.width() > printable.width() + 0.05 || expected.height() > printable.height() + 0.05)
            return std::unexpected(u"Этикетка %1 × %2 мм не помещается в область печати %3 × %4 мм. "
                u"Выберите больший формат бумаги или измените ориентацию в свойствах принтера."_s
                .arg(expected.width(), 0, 'f', 2).arg(expected.height(), 0, 'f', 2)
                .arg(printable.width(), 0, 'f', 2).arg(printable.height(), 0, 'f', 2));
    } else {
        const QPageSize page(expected, QPageSize::Millimeter, u"QtZpl label"_s, QPageSize::ExactMatch);
        QPageLayout layout(page, QPageLayout::Portrait, QMarginsF{}, QPageLayout::Millimeter);
        layout.setMode(QPageLayout::FullPageMode);
        if (!printer.setPageLayout(layout))
            return std::unexpected(u"Драйвер не принял размер этикетки. Настройте такой формат в свойствах принтера "
                u"или выберите печать на бумаге принтера."_s);
        printer.setFullPage(true);
        const auto actual = printer.pageLayout().fullRect(QPageLayout::Millimeter).size();
        if (std::abs(actual.width() - expected.width()) > 0.25 || std::abs(actual.height() - expected.height()) > 0.25)
            return std::unexpected(u"Драйвер заменил размер этикетки на %1 × %2 мм. "
                u"Установите формат %3 × %4 мм в свойствах принтера или выберите печать на бумаге принтера."_s
                .arg(actual.width(), 0, 'f', 2).arg(actual.height(), 0, 'f', 2)
                .arg(expected.width(), 0, 'f', 2).arg(expected.height(), 0, 'f', 2));
    }
    QPainter painter;
    if (!painter.begin(&printer)) return std::unexpected(u"Не удалось начать печать через драйвер."_s);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    for (int repeat = 0; repeat < job.repeats; ++repeat) {
        if (repeat > 0 && !printer.newPage()) {
            painter.end();
            printer.abort();
            return std::unexpected(u"Не удалось начать следующую этикетку. Проверьте очередь перед повторной печатью."_s);
        }
        painter.drawImage(printImageTargetRect(job, printer.logicalDpiX(), printer.logicalDpiY()),
                          job.image, QRectF(job.image.rect()));
    }
    if (!painter.end() || printer.printerState() == QPrinter::Error || printer.printerState() == QPrinter::Aborted)
        return std::unexpected(u"Драйвер сообщил об ошибке печати. Проверьте очередь принтера."_s);
    return PrintSubmission{0, 0, u"В очередь передано изображение этикетки, повторов: %1."_s.arg(job.repeats)};
}

std::expected<PrintSubmission, QString> submitLabelPrint(const PreparedLabelPrint& job,
                                                       const PrintDestination& destination)
{
    const auto error = validateJob(job);
    if (!error.isEmpty()) return std::unexpected(error);
    if (destination.connection == PrintConnection::Tcp) {
        const auto bytes = printWireBytes(job);
        if (!bytes) return std::unexpected(bytes.error());
        return submitTcp(*bytes, job.repeats, job.mode == LabelPrintMode::Png, destination);
    }
    if (job.mode == LabelPrintMode::Png)
        return std::unexpected(u"Отправка PNG доступна только по IP. Для установленного принтера выберите печать изображением."_s);
    if (destination.printerName.trimmed().isEmpty()) return std::unexpected(u"Выберите установленный принтер."_s);
    if (job.mode == LabelPrintMode::Image) {
        const auto info = QPrinterInfo::printerInfo(destination.printerName);
        if (info.isNull()) return std::unexpected(u"Установленный принтер не найден."_s);
        QPrinter printer(info, QPrinter::HighResolution);
        return paintLabelPrint(job, printer, destination.usePrinterPaper);
    }
#ifdef Q_OS_WIN
    const auto bytes = printWireBytes(job);
    if (!bytes) return std::unexpected(bytes.error());
    return submitRawWindows(*bytes, job.repeats, destination.printerName);
#else
    return std::unexpected(u"Отправка ZPL в системную очередь поддерживается в Windows. "
        u"Выберите прямое подключение по IP или печать изображением."_s);
#endif
}
} // namespace Designer
