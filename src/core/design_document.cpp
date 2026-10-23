#include "design_document.hpp"
#include "eac_mark.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

static void initializeSampleResources()
{
    Q_INIT_RESOURCE(designer_samples);
}

namespace Designer {
QString elementTypeName(ElementType type)
{
    switch (type) {
    case ElementType::Text: return QStringLiteral("Текст");
    case ElementType::Box: return QStringLiteral("Прямоугольник");
    case ElementType::Line: return QStringLiteral("Линия");
    case ElementType::Ellipse: return QStringLiteral("Эллипс");
    case ElementType::Circle: return QStringLiteral("Круг");
    case ElementType::Code128: return QStringLiteral("Code 128");
    case ElementType::QrCode: return QStringLiteral("QR-код");
    case ElementType::DataMatrix: return QStringLiteral("DataMatrix");
    case ElementType::Image: return QStringLiteral("Изображение");
    case ElementType::Table: return QStringLiteral("Таблица");
    case ElementType::Ean13: return QStringLiteral("EAN-13");
    case ElementType::Ean8: return QStringLiteral("EAN-8");
    case ElementType::UpcA: return QStringLiteral("UPC-A");
    case ElementType::UpcE: return QStringLiteral("UPC-E");
    case ElementType::Code39: return QStringLiteral("Code 39");
    case ElementType::Pdf417: return QStringLiteral("PDF417");
    case ElementType::Codabar: return QStringLiteral("Codabar");
    case ElementType::Code93: return QStringLiteral("Code 93");
    case ElementType::Interleaved2of5: return QStringLiteral("Interleaved 2 of 5");
    case ElementType::Industrial2of5: return QStringLiteral("Industrial 2 of 5");
    case ElementType::Gs1DataBar: return QStringLiteral("GS1 DataBar");
    case ElementType::MicroPdf417: return QStringLiteral("MicroPDF417");
    case ElementType::Aztec: return QStringLiteral("Aztec");
    case ElementType::MaxiCode: return QStringLiteral("MaxiCode");
    case ElementType::Diagonal: return QStringLiteral("Диагональная линия");
    case ElementType::Eac: return QStringLiteral("ЕАС");
    }
    return {};
}

bool isLinearBarcodeType(ElementType type)
{
    return type == ElementType::Code128 || (type >= ElementType::Ean13 && type <= ElementType::Code39)
        || type == ElementType::Codabar || type == ElementType::Code93
        || type == ElementType::Interleaved2of5 || type == ElementType::Industrial2of5;
}

bool isBarcodeType(ElementType type)
{
    return isLinearBarcodeType(type) || type == ElementType::QrCode
        || type == ElementType::DataMatrix || type == ElementType::Pdf417 || type == ElementType::Gs1DataBar
        || type == ElementType::MicroPdf417 || type == ElementType::Aztec || type == ElementType::MaxiCode;
}

QPoint mapDocumentPoint(const DesignDocument& document, QPoint point)
{
    if (document.printInverted != document.printMirror) point.setX(document.width - point.x());
    if (document.printInverted) point.setY(document.height - point.y());
    return point;
}

QPoint mapDocumentDelta(const DesignDocument& document, QPoint delta)
{
    if (document.printInverted != document.printMirror) delta.setX(-delta.x());
    if (document.printInverted) delta.setY(-delta.y());
    return delta;
}

QRect mapDocumentRect(const DesignDocument& document, QRect rect)
{
    if (document.printInverted != document.printMirror) rect.moveLeft(document.width - rect.x() - rect.width());
    if (document.printInverted) rect.moveTop(document.height - rect.y() - rect.height());
    return rect;
}

void resizeTableGrid(Element& table, int rows, int columns)
{
    rows = std::clamp(rows, 1, 12); columns = std::clamp(columns, 1, 12);
    const auto old = table;
    table.tableColumnWidths.resize(columns, 120);
    table.tableRowHeights.resize(rows, 48);
    table.tableCells = QStringList(rows * columns, QString{});
    for (int row = 0; row < std::min<int>(rows, old.tableRowHeights.size()); ++row)
        for (int col = 0; col < std::min<int>(columns, old.tableColumnWidths.size()); ++col)
            table.tableCells[row * columns + col] = old.tableCells.value(row * old.tableColumnWidths.size() + col);
    table.width = std::accumulate(table.tableColumnWidths.begin(), table.tableColumnWidths.end(), 0);
    table.height = std::accumulate(table.tableRowHeights.begin(), table.tableRowHeights.end(), 0);
}

void setTableDimensions(Element& table, int width, int height)
{
    const auto distribute = [](QList<int>& values, int total, int minimum) {
        if (values.isEmpty()) return;
        minimum = std::clamp(minimum, 8, 4096 / int(values.size()));
        total = std::clamp(total, int(values.size()) * minimum, 4096);
        const int oldTotal = std::accumulate(values.begin(), values.end(), 0);
        int remaining = total;
        for (qsizetype i = 0; i < values.size(); ++i) {
            const int remainingSlots = int(values.size() - i - 1);
            const int part = i + 1 == values.size() ? remaining
                : std::clamp(qRound(double(values[i]) * total / std::max(1, oldTotal)), minimum, remaining - remainingSlots * minimum);
            values[i] = part; remaining -= part;
        }
    };
    distribute(table.tableColumnWidths, width, std::max(table.thickness, 2 * table.tablePadding + 1));
    distribute(table.tableRowHeights, height, std::max(table.thickness, table.fontHeight + 2 * table.tablePadding));
    table.width = std::accumulate(table.tableColumnWidths.begin(), table.tableColumnWidths.end(), 0);
    table.height = std::accumulate(table.tableRowHeights.begin(), table.tableRowHeights.end(), 0);
}

QList<Element> tableElements(const Element& table)
{
    QList<Element> result;
    if (table.tableBorders) {
        auto border = makeElement(ElementType::Box, {table.x, table.y});
        border.width = table.width; border.height = table.height; border.thickness = table.thickness;
        result.append(border);
        int x = table.x;
        for (qsizetype col = 0; col + 1 < table.tableColumnWidths.size(); ++col) {
            x += table.tableColumnWidths[col];
            auto line = makeElement(ElementType::Line, {x, table.y});
            line.width = line.thickness = table.thickness; line.height = table.height;
            result.append(line);
        }
        int y = table.y;
        for (qsizetype row = 0; row + 1 < table.tableRowHeights.size(); ++row) {
            y += table.tableRowHeights[row];
            auto line = makeElement(ElementType::Line, {table.x, y});
            line.height = line.thickness = table.thickness; line.width = table.width;
            result.append(line);
        }
    }
    int y = table.y;
    for (qsizetype row = 0; row < table.tableRowHeights.size(); ++row) {
        int x = table.x;
        for (qsizetype col = 0; col < table.tableColumnWidths.size(); ++col) {
            auto cell = makeElement(ElementType::Text, {x + table.tablePadding, y + table.tablePadding});
            cell.text = table.tableCells.value(row * table.tableColumnWidths.size() + col);
            cell.literalText = table.literalText; cell.fontHeight = table.fontHeight; cell.fontWidth = table.fontWidth;
            cell.blockWidth = std::max(1, table.tableColumnWidths[col] - 2 * table.tablePadding);
            cell.blockMaxLines = std::clamp((table.tableRowHeights[row] - 2 * table.tablePadding)
                / std::max(1, table.fontHeight), 1, 128);
            cell.blockJustification = table.blockJustification;
            // Keep even empty cells as fields so the table's metadata has an
            // unambiguous field count and an editable source representation.
            result.append(cell);
            x += table.tableColumnWidths[col];
        }
        y += table.tableRowHeights[row];
    }
    // The table owns a single top-left anchor; document ^FW justification
    // must not independently shift every internal cell and grid line.
    for (auto& child : result) child.originJustification = 0;
    return result;
}

Element makeElement(ElementType type, QPoint position)
{
    Element element;
    element.type = type;
    element.name = elementTypeName(type);
    element.x = position.x();
    element.y = position.y();
    if (type == ElementType::Text) { element.text = QStringLiteral("Новый текст"); element.height = 40; }
    else if (type == ElementType::Line) { element.height = 2; }
    else if (type == ElementType::QrCode || type == ElementType::DataMatrix) {
        element.width = element.height = 100;
        element.gs1 = type == ElementType::DataMatrix;
        element.text = element.gs1 ? QStringLiteral("(01)09506000134352(21)SERIAL001")
                                  : QStringLiteral("https://example.com");
    } else if (type == ElementType::Circle) { element.width = element.height = 100; }
    else if (type == ElementType::Code128) { element.text = QStringLiteral("1234567890"); }
    else if (type == ElementType::Image) { element.width = element.height = 160; }
    else if (type == ElementType::Table) { element.width = 240; element.height = 96; element.fontHeight = 24; }
    else if (type == ElementType::Eac) { element.width = element.height = 90; }
    else if (type == ElementType::Ean13) { element.text = QStringLiteral("5901234123457"); element.moduleWidth = 2; }
    else if (type == ElementType::Ean8) { element.text = QStringLiteral("96385074"); element.moduleWidth = 2; }
    else if (type == ElementType::UpcA) { element.text = QStringLiteral("036000291452"); element.moduleWidth = 2; }
    else if (type == ElementType::UpcE) { element.text = QStringLiteral("04252614"); element.moduleWidth = 2; }
    else if (type == ElementType::Code39) { element.text = QStringLiteral("ABC-123"); element.moduleWidth = 2; element.humanReadable = false; }
    else if (type == ElementType::Codabar) { element.text = QStringLiteral("123456"); element.codabarStop = u'B'; element.humanReadable = false; element.moduleWidth = 2; }
    else if (type == ElementType::Pdf417) { element.text = QStringLiteral("PDF417 sample"); element.moduleWidth = 2; element.humanReadable = false; }
    else if (type == ElementType::Code93) { element.text = QStringLiteral("Code93-123"); element.moduleWidth = 2; }
    else if (type == ElementType::Interleaved2of5 || type == ElementType::Industrial2of5) { element.text = QStringLiteral("12345678"); element.moduleWidth = 2; }
    else if (type == ElementType::Gs1DataBar) { element.text = QStringLiteral("09506000134352"); element.moduleWidth = 2; element.height = 25; element.humanReadable = false; }
    else if (type == ElementType::MicroPdf417) { element.text = QStringLiteral("ABC"); element.moduleWidth = 2; element.pdf417RowHeight = 2; element.humanReadable = false; }
    else if (type == ElementType::Aztec) { element.text = QStringLiteral("Aztec sample"); element.moduleWidth = 3; element.humanReadable = false; }
    else if (type == ElementType::MaxiCode) { element.text = QStringLiteral("MaxiCode sample"); element.width = 201; element.height = 195; element.humanReadable = false; }
    return element;
}

namespace {
int roundedLimit(double value, int minimum, int maximum)
{
    return qRound(std::clamp(value, double(minimum), double(maximum)));
}

bool widthControlsUniformResize(QSize requested, QSize current)
{
    if (requested.width() == current.width()) return false;
    if (requested.height() == current.height()) return true;
    const double widthChange = std::abs(double(requested.width()) / current.width() - 1.0);
    const double heightChange = std::abs(double(requested.height()) / current.height() - 1.0);
    return widthChange >= heightChange;
}
} // namespace

Element resizedElement(const Element& element, QSize requestedSize, QSize currentRenderedSize)
{
    Element resized = element;
    requestedSize = {std::clamp(requestedSize.width(), 1, 4096),
                     std::clamp(requestedSize.height(), 1, 4096)};
    // Empty text and invalid barcode data can lack ink bounds. A model-size
    // fallback keeps editing available without another rendering implementation.
    if (currentRenderedSize.width() <= 0)
        currentRenderedSize.setWidth(std::max(1, element.width));
    if (currentRenderedSize.height() <= 0)
        currentRenderedSize.setHeight(std::max(1, element.height));
    if (requestedSize == currentRenderedSize) return resized;

    switch (element.type) {
    case ElementType::Table:
        setTableDimensions(resized, requestedSize.width(), requestedSize.height());
        break;
    case ElementType::Box:
    case ElementType::Line:
    case ElementType::Ellipse:
    case ElementType::Diagonal:
        resized.width = requestedSize.width();
        resized.height = requestedSize.height();
        break;
    case ElementType::Image: {
        if (element.imageKeepAspectRatio) {
            const double scale = widthControlsUniformResize(requestedSize, currentRenderedSize)
                ? double(requestedSize.width()) / currentRenderedSize.width()
                : double(requestedSize.height()) / currentRenderedSize.height();
            const double bounded = std::min({scale, 4096.0 / element.width, 4096.0 / element.height});
            resized.width = roundedLimit(element.width * bounded, 1, 4096);
            resized.height = roundedLimit(element.height * bounded, 1, 4096);
        } else {
            resized.width = requestedSize.width();
            resized.height = requestedSize.height();
        }
        break;
    }
    case ElementType::Eac: {
        const int side = widthControlsUniformResize(requestedSize, currentRenderedSize)
            ? requestedSize.width() : requestedSize.height();
        resized.width = resized.height = eacSizeForRequest(side, 203, element.eacStyle);
        break;
    }
    case ElementType::Circle:
        resized.width = resized.height = widthControlsUniformResize(requestedSize, currentRenderedSize)
            ? requestedSize.width() : requestedSize.height();
        break;
    case ElementType::Text:
    case ElementType::Code128:
    case ElementType::Ean13:
    case ElementType::Ean8:
    case ElementType::UpcA:
    case ElementType::UpcE:
    case ElementType::Code39:
    case ElementType::Codabar:
    case ElementType::Code93:
    case ElementType::Interleaved2of5:
    case ElementType::Industrial2of5:
    case ElementType::MicroPdf417:
    case ElementType::Pdf417: {
        if (element.orientation == u'R' || element.orientation == u'B') {
            requestedSize.transpose();
            currentRenderedSize.transpose();
        }
        const double scaleX = double(requestedSize.width()) / currentRenderedSize.width();
        const double scaleY = double(requestedSize.height()) / currentRenderedSize.height();
        if (isBarcodeType(element.type)) {
            resized.moduleWidth = roundedLimit(element.moduleWidth * scaleX, 1, 10);
            if (element.type == ElementType::Pdf417 || element.type == ElementType::MicroPdf417) {
                resized.pdf417RowHeight = roundedLimit(element.pdf417RowHeight * scaleY, 1, 64);
                break;
            }
            // The default interpretation scales with the module. Infer its
            // current ink height from QtZpl's bounds, retaining the fixed gap,
            // so a width gesture does not also make the entire code taller.
            const int oldExtra = element.humanReadable
                ? std::max(0, currentRenderedSize.height() - element.height) : 0;
            const int gap = std::min(5, oldExtra);
            const int newExtra = gap + qRound((oldExtra - gap)
                * double(resized.moduleWidth) / element.moduleWidth);
            resized.height = int(std::clamp(qint64(element.height) + requestedSize.height()
                - currentRenderedSize.height() - (newExtra - oldExtra), qint64(1), qint64(1024)));
            break;
        }

        if (element.blockWidth > 0) {
            resized.blockWidth = roundedLimit(element.blockWidth * scaleX, 1, 4096);
            resized.blockHangingIndent = roundedLimit(element.blockHangingIndent * scaleX, -4096, 4096);
            resized.blockLineSpacing = roundedLimit(element.blockLineSpacing * scaleY, -512, 512);
        }

        // Font 0 uses independent design-unit scales for height and width.
        // Automatic width follows the height only for a uniform resize.
        const int oldHeight = std::clamp(element.fontHeight, 1, 512);
        if (requestedSize.height() != currentRenderedSize.height())
            resized.fontHeight = roundedLimit(oldHeight * scaleY, 1, 512);
        const bool uniform = qint64(requestedSize.width()) * currentRenderedSize.height()
            == qint64(requestedSize.height()) * currentRenderedSize.width();
        if (element.fontWidth == 0 && uniform) {
            resized.fontWidth = 0;
        } else {
            const int effectiveWidth = element.fontWidth > 0 ? element.fontWidth : oldHeight;
            resized.fontWidth = roundedLimit(effectiveWidth * scaleX, 1, 512);
        }
        break;
    }
    case ElementType::QrCode:
    case ElementType::Gs1DataBar:
    case ElementType::Aztec:
    case ElementType::DataMatrix: {
        const bool useWidth = widthControlsUniformResize(requestedSize, currentRenderedSize);
        const double scale = useWidth
            ? double(requestedSize.width()) / currentRenderedSize.width()
            : double(requestedSize.height()) / currentRenderedSize.height();
        resized.moduleWidth = roundedLimit(element.moduleWidth * scale, 1, 10);
        break;
    }
    case ElementType::MaxiCode:
        // ^BD has a fixed physical dot geometry in QtZpl and Zebra ZPL.
        break;
    }
    return resized;
}

DesignDocument blankDocument()
{
    DesignDocument document;
    document.dpi = 300;
    document.width = document.height = qRound(100.0 * document.dpi / 25.4);
    return document;
}

DesignDocument sampleDocument()
{
    static const auto sampleImages = [] {
        initializeSampleResources();
        return std::pair{
            QImage(QStringLiteral(":/designer/samples/baltiysk-emblem.png")),
            QImage(QStringLiteral(":/designer/samples/maritime-anchor.png"))};
    }();

    // 100 x 100 mm at 300 dpi, the format of File > New. Every coordinate is
    // in printer dots for this resolution.
    auto document = blankDocument();
    const auto addText = [&document](const QString& value, QPoint position, int height,
                                    bool reverse = false) {
        auto element = makeElement(ElementType::Text, position);
        element.name = element.text = value;
        element.fontHeight = height;
        element.reverse = reverse;
        document.elements.append(element);
    };
    const auto addLine = [&document](QPoint position, QSize size, int thickness = 3) {
        auto element = makeElement(ElementType::Line, position);
        element.width = size.width(); element.height = size.height();
        element.thickness = thickness;
        document.elements.append(element);
    };

    auto frame = makeElement(ElementType::Box, {24, 24});
    frame.name = QStringLiteral("Рамка транспортной этикетки");
    frame.width = document.width - 48; frame.height = document.height - 48; frame.thickness = 4;
    document.elements.append(frame);

    addText(QStringLiteral("NORTHLINE"), {53, 62}, 71);
    addText(QStringLiteral("STUDIO / MARITIME LOGISTICS"), {56, 148}, 27);
    addText(QStringLiteral("TraceWay - Original Soft"), {56, 207}, 38);
    auto service = makeElement(ElementType::Box, {700, 53});
    service.name = QStringLiteral("Индекс сервиса — подложка");
    service.width = 142; service.height = 100; service.thickness = 100;
    document.elements.append(service);
    addText(QStringLiteral("B1"), {724, 68}, 80, true);
    addText(QStringLiteral("ЭКСПРЕСС"), {709, 189}, 30);
    auto emblem = makeElement(ElementType::Image, {896, 41});
    emblem.name = QStringLiteral("Морская эмблема — Балтийск");
    emblem.image = sampleImages.first;
    emblem.width = emblem.height = 231;
    document.elements.append(emblem);
    addLine({30, 272}, {1121, 4}, 4);

    addText(QStringLiteral("ПОРТ ОТПРАВЛЕНИЯ"), {53, 297}, 24);
    addText(QStringLiteral("Балтийск"), {53, 341}, 77);
    addText(QStringLiteral("СОРТИРОВОЧНЫЙ МАРШРУТ"), {671, 297}, 24);
    addText(QStringLiteral("BLT / 01"), {671, 343}, 71);
    addLine({30, 423}, {1121, 4}, 4);
    addLine({638, 278}, {3, 567});

    addText(QStringLiteral("ОТПРАВИТЕЛЬ / ОТ"), {53, 452}, 24);
    addText(QStringLiteral("NORTHLINE STUDIO"), {53, 500}, 38);
    addText(QStringLiteral("Балтийск / Северный терминал"), {53, 553}, 30);
    addText(QStringLiteral("ПРИЧАЛ 07 / СЕКТОР B1"), {53, 597}, 27);
    addLine({53, 643}, {556, 1}, 1);
    addText(QStringLiteral("ПОЛУЧАТЕЛЬ / КОМУ"), {53, 674}, 24);
    addText(QStringLiteral("TRACEWAY ORIGINAL SOFT"), {53, 718}, 38);
    addText(QStringLiteral("МОРСКАЯ КОЛЛЕКЦИЯ / 01"), {53, 771}, 27);
    addText(QStringLiteral("ОТПРАВЛЕНИЕ NL-00001"), {53, 813}, 27);

    auto anchor = makeElement(ElementType::Image, {686, 432});
    anchor.name = QStringLiteral("Классический морской якорь");
    anchor.image = sampleImages.second;
    anchor.width = anchor.height = 420;
    anchor.dither = false;
    document.elements.append(anchor);
    addLine({30, 863}, {1121, 4}, 4);

    const QList<int> columns{53, 337, 621, 904};
    const QStringList captions{QStringLiteral("МЕСТ"), QStringLiteral("ВЕС"),
                               QStringLiteral("ФОРМАТ"), QStringLiteral("ТИП")};
    const QStringList values{QStringLiteral("01 / 01"), QStringLiteral("0,70 КГ"),
                             QStringLiteral("100 x 100"), QStringLiteral("ORIGINAL")};
    for (qsizetype index = 0; index < columns.size(); ++index) {
        addText(captions[index], {columns[index], 893}, 21);
        addText(values[index], {columns[index], 931}, 35);
        if (index > 0) addLine({columns[index] - 30, 869}, {1, 103}, 1);
    }
    addLine({30, 978}, {1121, 4}, 4);

    auto code = makeElement(ElementType::Code128, {53, 1014});
    code.height = 71; code.moduleWidth = 3; code.text = QStringLiteral("NL-00001");
    document.elements.append(code);
    auto manifest = makeElement(ElementType::Pdf417, {464, 1011});
    manifest.name = QStringLiteral("Транспортный манифест PDF417");
    manifest.text = QStringLiteral("TRACEWAY|DEMO|NL-00001|BLT|B1|1/1|0.70KG");
    manifest.moduleWidth = 3; manifest.pdf417Columns = 3;
    manifest.pdf417RowHeight = 6; manifest.pdf417Security = 2;
    document.elements.append(manifest);
    auto dataMatrix = makeElement(ElementType::DataMatrix, {981, 1005});
    dataMatrix.text = QStringLiteral("(01)09506000134352(21)NL-00001"); dataMatrix.moduleWidth = 7;
    document.elements.append(dataMatrix);
    auto eac = makeElement(ElementType::Eac, {1054, 322});
    eac.name = QStringLiteral("ЕАС — единый знак обращения");
    eac.width = eac.height = 81;
    document.elements.append(eac);
    addText(QStringLiteral("ДЕМО / МОРСКАЯ КОЛЛЕКЦИЯ / 0001"), {464, 1120}, 18);
    return document;
}

} // namespace Designer
