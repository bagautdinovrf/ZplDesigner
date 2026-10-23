#include "design_document.hpp"
#include "design_internal.hpp"
#include "gs1_data.hpp"
#include "value_placeholders.hpp"
#include "data_matrix_values.hpp"
#include "eac_mark.hpp"

#include <algorithm>
#include <type_traits>
#include <cmath>
#include <numeric>

namespace Designer::Internal {
using namespace Qt::StringLiterals;
bool safeSize(int width, int height)
{
    return width > 0 && height > 0 && width <= maximumSide && height <= maximumSide
        && qint64(width) * height <= maximumPixels;
}

bool safeCoordinate(int value) { return value >= -maximumSide && value <= maximumSide; }

QString diagnosticText(const QtZpl::Diagnostic& value)
{
    return u"%1: %2"_s.arg(value.code, value.message);
}

QString validateElement(const Element& element, bool preservePlaceholders, int dpi)
{
    if (!safeCoordinate(element.x) || !safeCoordinate(element.y)
        || !safeSize(element.width, element.height))
        return u"Координаты или размер элемента превышают предел 4096 точек."_s;
    if (element.fontHeight < 1 || element.fontHeight > 512 || element.fontWidth < 0
        || element.fontWidth > 512 || element.thickness < 1 || element.thickness > 4096
        || element.moduleWidth < 1 || element.moduleWidth > 10
        || element.text.size() > maximumField)
        return u"Параметры элемента превышают безопасные пределы MVP."_s;
    if (!QStringView{u"NRIB"}.contains(element.orientation))
        return u"Некорректная ориентация элемента."_s;
    if (element.originJustification < -1 || element.originJustification > 2
        || !QStringView{u"HVR"}.contains(element.textDirection)
        || element.characterSpacing < -10 || element.characterSpacing > 9999
        || !QStringView{u"LR"}.contains(element.diagonalDirection))
        return u"Недопустимые параметры начала поля, направления текста, интервала или диагонали."_s;
    if (element.type == ElementType::Text
        && (2 * qint64(std::max(element.fontHeight, element.fontWidth)) + std::abs(element.characterSpacing))
            * element.fontHeight * std::max<qsizetype>(1, element.text.size()) > maximumPixels)
        return u"Текст с выбранным интервалом превышает безопасный размер изображения."_s;
    if (element.blockWidth < 0 || element.blockWidth > maximumSide
        || element.blockMaxLines < 1 || element.blockMaxLines > 128
        || std::abs(qint64(element.blockLineSpacing)) > 512
        || std::abs(qint64(element.blockHangingIndent)) > maximumSide
        || !QStringView{u"LCRJ"}.contains(element.blockJustification)
        || !QStringView{u"NABCDU"}.contains(element.code128Mode)
        || !std::isfinite(element.barcodeRatio) || element.barcodeRatio < 2 || element.barcodeRatio > 3)
        return u"Некорректные параметры текстового блока или Code 128."_s;
    if (!QStringView{u"LMQH"}.contains(element.qrErrorCorrection)
        || element.pdf417Columns < 0 || element.pdf417Columns > 30
        || element.pdf417Security < 0 || element.pdf417Security > 8
        || element.pdf417RowHeight < 1 || element.pdf417RowHeight > 64)
        return u"Некорректные параметры QR/PDF417."_s;
    if (element.pdf417Rows < 0 || element.pdf417Rows > 90
        || (element.pdf417Rows != 0 && element.pdf417Rows < 3)
        || element.dataMatrixRows < 0 || element.dataMatrixRows > 144
        || element.dataMatrixColumns < 0 || element.dataMatrixColumns > 144
        || element.dataBarType < 1 || element.dataBarType > 6
        || element.dataBarSegments < 0 || element.dataBarSegments > 22 || (element.dataBarSegments % 2 != 0)
        || element.dataBarSeparator < 1 || element.dataBarSeparator > 2
        || element.microPdf417Mode < 0 || element.microPdf417Mode > 33
        || !((element.aztecSize >= 1 && element.aztecSize <= 99)
            || (element.aztecSize >= 101 && element.aztecSize <= 104)
            || (element.aztecSize >= 201 && element.aztecSize <= 232) || element.aztecSize == 300)
        || element.maxiCodeMode < 2 || element.maxiCodeMode > 6
        || element.maxiCodePosition < 1 || element.maxiCodePosition > element.maxiCodeTotal || element.maxiCodeTotal > 8)
        return u"Некорректные расширенные параметры штрихкода."_s;
    if (element.type == ElementType::MaxiCode && element.orientation != u'N')
        return u"MaxiCode имеет фиксированный размер и ориентацию ^BD."_s;
    if (element.type == ElementType::Eac) {
        if (static_cast<unsigned>(element.eacStyle) > static_cast<unsigned>(EacStyle::WhiteTransparent)
            || element.width != element.height || element.width % eacGridUnits(element.eacStyle) != 0
            || element.orientation != u'N' || element.reverse)
            return u"Знак ЕАС должен сохранять квадратную сетку 9 × 9, без поворота и произвольной инверсии."_s;
        if (dpi > 0 && element.width < eacMinimumSize(dpi, element.eacStyle))
            return u"Базовый размер букв ЕАС должен быть не менее 5 мм при выбранном DPI."_s;
        for (const auto& child : eacElements(element)) {
            const auto error = validateElement(child, preservePlaceholders);
            if (!error.isEmpty()) return error;
        }
    }
    if (element.type == ElementType::Table) {
        if (element.tableColumnWidths.isEmpty() || element.tableColumnWidths.size() > 12
            || element.tableRowHeights.isEmpty() || element.tableRowHeights.size() > 12
            || element.tableCells.size() != element.tableColumnWidths.size() * element.tableRowHeights.size()
            || element.tablePadding < 0 || element.tablePadding > 128 || element.orientation != u'N' || element.reverse)
            return u"Некорректная таблица: предел — 12 строк × 12 колонок, без поворота и инверсии."_s;
        const auto badSize = [](int value) { return value < 8 || value > 4096; };
        if (std::ranges::any_of(element.tableColumnWidths, badSize) || std::ranges::any_of(element.tableRowHeights, badSize)
            || std::accumulate(element.tableColumnWidths.begin(), element.tableColumnWidths.end(), qint64{0}) != element.width
            || std::accumulate(element.tableRowHeights.begin(), element.tableRowHeights.end(), qint64{0}) != element.height)
            return u"Размер таблицы должен совпадать с суммой размеров строк и колонок."_s;
        if (std::ranges::any_of(element.tableColumnWidths, [&](int width) {
                return width <= 2 * element.tablePadding || (element.tableBorders && width < element.thickness);
            }) || std::ranges::any_of(element.tableRowHeights, [&](int height) {
                return height < element.fontHeight + 2 * element.tablePadding || (element.tableBorders && height < element.thickness);
            }))
            return u"В ячейке должны помещаться высота букв, отступы и толщина границ."_s;
        for (const auto& child : tableElements(element)) {
            const auto error = validateElement(child, preservePlaceholders);
            if (!error.isEmpty()) return error;
        }
    }
    if (element.type == ElementType::DataMatrix && element.gs1) {
        const auto data = preservePlaceholders ? parseGs1Template(element.text) : parseGs1(element.text);
        if (!data) return data.error();
    } else if (supportsValueParameters(element.type) && preservePlaceholders && !element.literalText) {
        const auto names = requiredValueParameters(element.text);
        if (!names) return names.error();
    }
    if (element.type == ElementType::Text
        && qint64(std::max(element.fontHeight, element.fontWidth))
            * element.fontHeight * std::max<qsizetype>(1, element.text.size()) > maximumPixels / 2)
        return u"Текст слишком велик для безопасного рендеринга."_s;
    if (isLinearBarcodeType(element.type)
        && qint64(element.text.size() + 8) * 11 * element.moduleWidth * (element.height + 160) > maximumPixels)
        return u"Штрихкод слишком велик для безопасного рендеринга."_s;
    // Template values are checked after substitution, before printing.
    const bool concreteText = !preservePlaceholders || element.literalText
        || (!element.text.contains(u"{{") && !element.text.contains(u"[["));
    const auto onlyCharacters = [&](QStringView allowed) {
        return !element.text.isEmpty() && std::ranges::all_of(element.text, [allowed](QChar ch) { return allowed.contains(ch); });
    };
    if (element.type == ElementType::Code39 && concreteText && !onlyCharacters(u"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ-. "))
        return u"Code 39: доступные символы — цифры, A–Z, пробел, дефис и точка."_s;
    // QtZpl, Labelary and printers take start/stop only from ^BK k and l and
    // drop other characters with codabar-data-ignored.
    if (element.type == ElementType::Codabar) {
        if (!QStringView{u"ABCD"}.contains(element.codabarStart) || !QStringView{u"ABCD"}.contains(element.codabarStop))
            return u"Codabar: старт и стоп — символы A–D."_s;
        if (concreteText && !onlyCharacters(u"0123456789-$:/.+"))
            return u"Codabar: в данных допустимы только цифры и - $ : / . +. Старт и стоп A–D задаются "
                   u"параметрами кода (^BK k,l); другие символы данных принтер пропускает."_s;
    }
    // QtZpl reports barcode-interpretation-pending instead of drawing these
    // captions (canary: code39CaptionStillPendingInQtZpl).
    if ((element.type == ElementType::Code39 || element.type == ElementType::Codabar)
        && (element.humanReadable || element.interpretationAbove))
        return u"Подпись Code 39/Codabar пока не поддерживается QtZpl; добавьте отдельный текст."_s;
    if (element.type == ElementType::Image
        && (element.image.isNull() || !safeSize(element.image.width(), element.image.height())))
        return u"Изображение отсутствует или превышает предел 4096 × 4096."_s;
    return {};
}

// Check dimensions before entering the renderer, including dimensions of temporary
// field images. The library itself intentionally accepts larger printer labels.
QString validateParsed(const QtZpl::Document& parsed, qsizetype* errorOffset)
{
    if (errorOffset) *errorOffset = -1;
    if (parsed.labels().isEmpty())
        return u"В файле нет этикетки ^XA…^XZ."_s;
    if (parsed.labels().size() > 16)
        return u"MVP открывает не более 16 этикеток в одном файле."_s;
    for (const auto& diagnostic : parsed.diagnostics())
        if (diagnostic.code == u"nested-format" || diagnostic.code == u"unterminated-format") {
            if (errorOffset) *errorOffset = diagnostic.offset;
            return diagnosticText(diagnostic);
        }
    for (const auto& label : parsed.labels()) {
        if (!safeSize(label.width() > 0 ? label.width() : 812,
                      label.height() > 0 ? label.height() : 1218)) {
            if (errorOffset) {
                for (const auto& command : label.commands()) {
                    const auto* width = std::get_if<QtZpl::PrintWidth>(&command.payload);
                    const auto* height = std::get_if<QtZpl::LabelLength>(&command.payload);
                    if ((width && width->dots > maximumSide) || (height && height->dots > maximumSide)) {
                        *errorOffset = command.sourceSpan.start;
                        break;
                    }
                }
            }
            return u"Размер этикетки превышает 4096 × 4096 точек."_s;
        }
        if (label.commands().size() > 2400) {
            if (errorOffset) *errorOffset = label.commands()[2400].sourceSpan.start;
            return u"Слишком много команд: предел MVP — 2400 на этикетку."_s;
        }
        int fontHeight = 30;
        int fontWidth = 0;
        int barcodeModule = 2;
        int barcodeHeight = 10;
        int characterSpacing = 0;
        bool pendingBarcode = false;
        for (const auto& command : label.commands()) {
            bool safe = true;
            std::visit([&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, QtZpl::PrintWidth> || std::is_same_v<T, QtZpl::LabelLength>)
                    safe = value.dots > 0 && value.dots <= maximumSide;
                else if constexpr (std::is_same_v<T, QtZpl::FieldOrigin> || std::is_same_v<T, QtZpl::FieldTypeset>
                                   || std::is_same_v<T, QtZpl::LabelHome>)
                    safe = safeCoordinate(value.x) && safeCoordinate(value.y);
                else if constexpr (std::is_same_v<T, QtZpl::LabelShift>)
                    safe = safeCoordinate(value.dots);
                else if constexpr (std::is_same_v<T, QtZpl::FieldParameter>) {
                    safe = value.spacing >= -10 && value.spacing <= 9999 && QStringView{u"HVR"}.contains(value.direction);
                    characterSpacing = value.spacing;
                }
                else if constexpr (std::is_same_v<T, QtZpl::FieldSeparator>) characterSpacing = 0;
                else if constexpr (std::is_same_v<T, QtZpl::ScalableFont> || std::is_same_v<T, QtZpl::ChangeFont>) {
                    safe = value.height >= 0 && value.height <= 512 && value.width >= 0 && value.width <= 512;
                    fontHeight = std::max(30, value.height);
                    fontWidth = value.width;
                } else if constexpr (std::is_same_v<T, QtZpl::FieldBlock>) {
                    safe = value.width > 0 && value.width <= maximumSide && value.maxLines > 0
                        && value.maxLines <= 128 && std::abs(qint64(value.lineSpacing)) <= 512
                        && std::abs(qint64(value.hangingIndent)) <= maximumSide
                        && qint64(value.width) * value.maxLines
                            * (fontHeight + std::max(0, value.lineSpacing) + 64) <= maximumPixels;
                } else if constexpr (std::is_same_v<T, QtZpl::BarcodeDefault>) {
                    safe = value.moduleWidth >= 1 && value.moduleWidth <= 10 && value.height >= 0
                        && value.height <= 1024 && value.wideToNarrowRatio > 0 && value.wideToNarrowRatio <= 10;
                    barcodeModule = value.moduleWidth;
                    barcodeHeight = value.height;
                } else if constexpr (std::is_same_v<T, QtZpl::GraphicBox> || std::is_same_v<T, QtZpl::GraphicEllipse>
                                     || std::is_same_v<T, QtZpl::GraphicDiagonal>) {
                    safe = value.width >= 0 && value.width <= maximumSide && value.height >= 0
                        && value.height <= maximumSide && value.thickness > 0 && value.thickness <= maximumSide;
                } else if constexpr (std::is_same_v<T, QtZpl::GraphicCircle>) {
                    safe = value.diameter > 0 && value.diameter <= maximumSide
                        && value.thickness > 0 && value.thickness <= maximumSide;
                } else if constexpr (std::is_same_v<T, QtZpl::GraphicField>) {
                    safe = value.totalBytes > 0 && value.totalBytes <= maximumPixels / 8
                        && value.bytesUsed > 0 && value.bytesUsed <= maximumSource
                        && value.bytesPerRow > 0 && value.bytesPerRow <= maximumSide / 8
                        && value.totalBytes % value.bytesPerRow == 0
                        && value.totalBytes / value.bytesPerRow <= maximumSide;
                    // Z64 needs no separate input cap: QtZpl inflates into a buffer
                    // of the declared size checked above, and every render applies
                    // the maximumGraphicBytes budget to all decoded graphics.
                } else if constexpr (std::is_same_v<T, QtZpl::Barcode>) {
                    pendingBarcode = true;
                    // Parameters have symbology-specific meanings. Bound every numeric
                    // parameter; stricter checks below cover matrix module multipliers.
                    for (const auto& parameter : value.parameters) {
                        bool numeric = false;
                        const qlonglong number = parameter.toLongLong(&numeric);
                        if (numeric && (number < 0 || number > 1024)) safe = false;
                    }
                    if (value.symbology == u"BQ") {
                        const int module = value.parameters.value(2, u"2"_s).toInt();
                        safe = safe && module >= 1 && module <= 10;
                    } else if (value.symbology == u"BX") {
                        const int module = value.parameters.value(1, u"3"_s).toInt();
                        safe = safe && module >= 1 && module <= 10;
                    } else if (value.symbology == u"B7") {
                        const int rowHeight = value.parameters.size() > 1 && !value.parameters[1].isEmpty()
                            ? value.parameters[1].toInt() : barcodeModule * 3;
                        safe = safe && rowHeight > 0
                            && qint64(581) * barcodeModule * 90 * rowHeight <= maximumPixels;
                    } else {
                        bool ok = false;
                        const int heightIndex = value.symbology == u"B3" || value.symbology == u"BK" ? 2 : 1;
                        const int height = value.parameters.value(heightIndex).toInt(&ok);
                        if (ok) barcodeHeight = height;
                    }
                } else if constexpr (std::is_same_v<T, QtZpl::FieldData>) {
                    safe = value.data.size() <= maximumField;
                    if (pendingBarcode)
                        safe = safe && qint64(value.data.size() + 8) * 18 * barcodeModule
                            * (std::max(1, barcodeHeight) + 160) <= maximumPixels;
                    else
                        safe = safe && (2 * qint64(std::max(fontHeight, fontWidth)) + std::abs(characterSpacing)) * fontHeight
                            * std::max<qsizetype>(1, value.data.size()) <= maximumPixels;
                    pendingBarcode = false;
                }
            }, command.payload);
            if (!safe) {
                if (errorOffset) *errorOffset = command.sourceSpan.start;
                return u"Небезопасные параметры команды в позиции %1: %2"_s.arg(command.sourceSpan.start)
                    .arg(parsed.sourceText(command.sourceSpan).trimmed().left(40));
            }
        }
    }
    return {};
}

const QString& TemplateSource::text() const
{
    if (!text_) text_ = toZpl(document_, preservePlaceholders_);
    return *text_;
}

std::expected<void, QString> validateTemplate(const TemplateSource& templateSource)
{
    const auto& document = templateSource.document();
    const bool preservePlaceholders = templateSource.preservePlaceholders();
    if (!QStringView{u"NRIB"}.contains(document.defaultOrientation)
        || document.defaultJustification < 0 || document.defaultJustification > 2)
        return std::unexpected(u"Недопустимые общие параметры направления или выравнивания полей."_s);
    if (document.originalZpl.isEmpty()) {
        if (!safeSize(document.width, document.height))
            return std::unexpected(u"Размер этикетки превышает 4096 × 4096 точек."_s);
        if (document.elements.size() > maximumElements)
            return std::unexpected(u"MVP поддерживает не более 200 элементов."_s);
        qint64 graphicBytes = 0;
        for (const auto& element : document.elements) {
            if (!element.printable) continue;
            const QString error = validateElement(element, preservePlaceholders, document.dpi);
            if (!error.isEmpty()) return std::unexpected(error);
            if (element.type == ElementType::Image) {
                // Reject guaranteed overflow before raster conversion/allocation.
                graphicBytes += qint64((element.width + 7) / 8) * element.height * 2 + 20;
                if (graphicBytes > maximumSource)
                    return std::unexpected(u"Графика создаёт ZPL больше 8 МиБ. Уменьшите изображения."_s);
            }
        }
    }
    // Serialized only after the element checks bound raster conversion.
    const QString& source = templateSource.text();
    if (source.isEmpty())
        return std::unexpected(u"Не удалось сохранить метаданные объектов: исходные изображения или данные проекта слишком велики."_s);
    if (source.size() > maximumSource || source.toUtf8().size() > maximumSource)
        return std::unexpected(u"ZPL превышает предел 8 МиБ в UTF-8."_s);
    const auto parsed = parseZpl(source);
    if (!parsed) return std::unexpected(parsed.error().message);
    const QString error = validateParsed(*parsed);
    if (!error.isEmpty()) return std::unexpected(error);
    if (!preservePlaceholders) {
        const auto rendered = checkedRender(source, document.dpi);
        if (!rendered) return std::unexpected(rendered.error());
        for (const auto& diagnostic : rendered->diagnostics)
            if (diagnostic.severity == QtZpl::Severity::Error)
                return std::unexpected(diagnosticText(diagnostic));
    }
    return {};
}
} // namespace Designer::Internal

namespace Designer {
std::expected<void, QString> validateForExport(const DesignDocument& document, bool preservePlaceholders)
{
    return Internal::validateTemplate(Internal::TemplateSource(document, preservePlaceholders));
}

std::expected<QString, QString> exportableZpl(const DesignDocument& document)
{
    const Internal::TemplateSource source(document);
    if (const auto valid = Internal::validateTemplate(source); !valid) return std::unexpected(valid.error());
    return source.text();
}
} // namespace Designer
